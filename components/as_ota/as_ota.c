#include "as_ota.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "as_config.h"
#include "as_log.h"
#include "as_ota_public_key.h"
#include "sodium/crypto_sign_ed25519.h"

static const char *TAG __attribute__((unused)) = "as_ota";

#define TRAILER (AS_OTA_VERSION_FIELD + AS_OTA_SIGNATURE)
#define SELF_TEST_TIMEOUT_MS (2 * 60 * 1000)
#define SELF_TEST_POLL_MS 500
#define HTTP_TIMEOUT_MS 20000
#define HTTP_CHUNK 4096
#define MAX_REDIRECTS 5
#define MIN_IMAGE_BYTES (64 * 1024)
#define FIRST_CHECK_DELAY_MS (2 * 60 * 1000)
#define CHECK_INTERVAL_MS (24UL * 60UL * 60UL * 1000UL)
#define CHECK_RETRY_MS (60 * 60 * 1000)
#define MAX_API_RESPONSE (48 * 1024)
// Where the app descriptor sits in an image: after the image header and the first segment header,
// which is also where esp_ota_get_partition_description reads it from.
#define APP_DESC_OFFSET 32
#define APP_DESC_END (APP_DESC_OFFSET + sizeof(esp_app_desc_t))

static volatile bool s_in_progress;
static volatile bool s_allow_downgrade;   // the next install may be older than the running firmware
static bool is_newer(const char *candidate, const char *current);
static volatile bool s_service_up;
static volatile bool s_self_test_pending;
static as_ota_update_info_t s_info;
static SemaphoreHandle_t s_info_lock;
static char s_repo[80] = "DMYTROSKORIN/esp32-air-sound";
static char s_tag_prefix[16] = "v";
static char s_asset[48] = "esp32-air-sound-signed.bin";

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }

static void set_result(as_ota_result_t *r, bool ok, const char *fmt, ...)
{
    r->ok = ok;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->message, sizeof(r->message), fmt, ap);
    va_end(ap);
}

const char *as_ota_running_version(void)
{
    return esp_app_get_description()->version;
}

bool as_ota_in_progress(void) { return s_in_progress; }

// ---------------------------------------------------------------------------
// Streaming sink
// ---------------------------------------------------------------------------
typedef struct {
    const esp_partition_t *target;
    esp_ota_handle_t handle;
    bool active;
    mbedtls_sha256_context sha;
    bool sha_active;
    uint8_t tail[TRAILER];
    size_t tail_len;
    size_t image_bytes;
    size_t received;
    bool start_checked;
    uint8_t head[APP_DESC_END];   // the image's first bytes, held back until the descriptor in them is read
    size_t head_len;
    bool allow_downgrade;
} sink_t;

static bool sink_write(sink_t *s, const uint8_t *data, size_t len, as_ota_result_t *r)
{
    mbedtls_sha256_update(&s->sha, data, len);
    esp_err_t err = esp_ota_write(s->handle, data, len);
    if (err != ESP_OK) {
        set_result(r, false, "flash write failed at %u B: %s", (unsigned)s->image_bytes, esp_err_to_name(err));
        return false;
    }
    s->image_bytes += len;
    return true;
}

// What the image's first bytes say, before any of them reach the flash. The slot is erased as it is
// written, so a refusal here leaves the previous image in it, whereas a refusal after esp_ota_end
// leaves a complete, bootable image of the wrong kind there - one a rollback would happily boot.
static bool sink_check_head(sink_t *s, as_ota_result_t *r)
{
    if (s->head[0] != 0xE9) {
        set_result(r, false, "not an ESP32 app image (bad magic 0x%02x)", s->head[0]);
        return false;
    }
    esp_app_desc_t in;
    memcpy(&in, s->head + APP_DESC_OFFSET, sizeof(in));
    if (in.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        set_result(r, false, "not an ESP-IDF app image (no app descriptor)");
        return false;
    }
    // The release key signs every board's images alike, so the signature says "ours", not "for this
    // board": an image built for the other board would boot with the wrong pins. The project name in
    // the image's app description is what says which board it is for.
    const esp_app_desc_t *self = esp_app_get_description();
    if (strncmp(in.project_name, self->project_name, sizeof(in.project_name)) != 0) {
        set_result(r, false, "image is for '%.32s', this board runs '%.32s': not installed", in.project_name,
                   self->project_name);
        return false;
    }
    // The descriptor's version is under the signature too, and it is what the running build reports;
    // an older signed image brings back whatever it was superseded for, unless the owner asked for it.
    char version[sizeof(in.version) + 1];
    memcpy(version, in.version, sizeof(in.version));
    version[sizeof(in.version)] = '\0';
    if (!s->allow_downgrade && is_newer(self->version, version)) {
        set_result(r, false, "image v%s is older than the running v%s: not installed (pass downgrade to go back)",
                   version, self->version);
        return false;
    }
    return true;
}

static bool sink_commit(sink_t *s, const uint8_t *data, size_t len, as_ota_result_t *r)
{
    if (!s->start_checked) {
        // The stream arrives in whatever pieces the download and the trailer hold-back make of it;
        // the descriptor ends at byte 288, so the start is collected until it is all there.
        size_t take = len < APP_DESC_END - s->head_len ? len : APP_DESC_END - s->head_len;
        memcpy(s->head + s->head_len, data, take);
        s->head_len += take;
        data += take;
        len -= take;
        if (s->head_len < APP_DESC_END) return true;
        if (!sink_check_head(s, r)) return false;
        s->start_checked = true;
        if (!sink_write(s, s->head, APP_DESC_END, r)) return false;
    }
    return len ? sink_write(s, data, len, r) : true;
}

static void sink_abort(sink_t *s)
{
    if (s->sha_active) {
        mbedtls_sha256_free(&s->sha);
        s->sha_active = false;
    }
    if (s->active) {
        esp_ota_abort(s->handle);
        s->active = false;
    }
}

static bool sink_begin(sink_t *s, as_ota_result_t *r)
{
    memset(s, 0, sizeof(*s));
    const esp_partition_t *running = esp_ota_get_running_partition();
    s->target = esp_ota_get_next_update_partition(NULL);
    if (!s->target || !running || s->target->address == running->address) {
        set_result(r, false, "no inactive OTA slot in the partition table");
        return false;
    }
    r->target_label = s->target->label;
    esp_err_t err = esp_ota_begin(s->target, OTA_WITH_SEQUENTIAL_WRITES, &s->handle);
    if (err != ESP_OK) {
        set_result(r, false, "esp_ota_begin(%s) failed: %s", s->target->label, esp_err_to_name(err));
        return false;
    }
    s->active = true;
    mbedtls_sha256_init(&s->sha);
    mbedtls_sha256_starts(&s->sha, 0);
    s->sha_active = true;
    return true;
}

static bool sink_feed(sink_t *s, const uint8_t *data, size_t len, as_ota_result_t *r)
{
    s->received += len;
    while (len > 0) {
        if (s->tail_len < TRAILER) {
            size_t take = len < TRAILER - s->tail_len ? len : TRAILER - s->tail_len;
            memcpy(s->tail + s->tail_len, data, take);
            s->tail_len += take;
            data += take;
            len -= take;
            continue;
        }
        size_t displace = len < TRAILER ? len : TRAILER;
        if (!sink_commit(s, s->tail, displace, r)) return false;
        memmove(s->tail, s->tail + displace, TRAILER - displace);
        s->tail_len = TRAILER - displace;
        if (len > TRAILER) {
            size_t direct = len - TRAILER;
            if (!sink_commit(s, data, direct, r)) return false;
            data += direct;
            len -= direct;
        }
    }
    return true;
}

static bool sink_finish(sink_t *s, as_ota_result_t *r)
{
    if (s->tail_len < TRAILER || s->image_bytes < MIN_IMAGE_BYTES) {
        set_result(r, false, "image too short (%u B): not a signed firmware file", (unsigned)s->received);
        sink_abort(s);
        return false;
    }
    const uint8_t *version = s->tail;
    const uint8_t *signature = s->tail + AS_OTA_VERSION_FIELD;
    memcpy(r->version, version, AS_OTA_VERSION_FIELD);
    r->version[AS_OTA_VERSION_FIELD] = '\0';
    for (char *c = r->version; *c; ++c) {
        if (*c < 0x20 || *c > 0x7e) {
            *c = '\0';
            break;
        }
    }
    mbedtls_sha256_update(&s->sha, version, AS_OTA_VERSION_FIELD);
    uint8_t digest[32];
    mbedtls_sha256_finish(&s->sha, digest);
    mbedtls_sha256_free(&s->sha);
    s->sha_active = false;
    if (crypto_sign_ed25519_verify_detached(signature, digest, sizeof(digest), as_ota_public_key) != 0) {
        set_result(r, false, "signature check FAILED: not signed with this firmware's release key (version '%s')",
                   r->version);
        sink_abort(s);
        return false;
    }
    esp_err_t err = esp_ota_end(s->handle);
    s->active = false;
    if (err != ESP_OK) {
        set_result(r, false, "image rejected by esp_ota_end: %s", esp_err_to_name(err));
        return false;
    }
    // The board check ran on the first bytes before anything was written (sink_check_head); this reads
    // the descriptor back from the flash, so a write that went astray cannot leave a foreign image in.
    esp_app_desc_t incoming = {0};
    const esp_app_desc_t *self = esp_app_get_description();
    if (esp_ota_get_partition_description(s->target, &incoming) != ESP_OK) {
        set_result(r, false, "could not read the app descriptor of %s", s->target->label);
        return false;
    }
    if (strncmp(incoming.project_name, self->project_name, sizeof(incoming.project_name)) != 0) {
        set_result(r, false, "image is for '%.32s', this board runs '%.32s': not installed", incoming.project_name,
                   self->project_name);
        return false;
    }
    err = esp_ota_set_boot_partition(s->target);
    if (err != ESP_OK) {
        set_result(r, false, "could not set boot partition: %s", esp_err_to_name(err));
        return false;
    }
    r->image_bytes = s->image_bytes;
    set_result(r, true, "verified %s (%u KB) written to %s", r->version, (unsigned)(s->image_bytes / 1024),
               s->target->label);
    as_logf("ota: installed %s into %s (replacing %s), awaiting first boot", r->version, s->target->label,
            as_ota_running_version());
    return true;
}

// ---------------------------------------------------------------------------
// Download
// ---------------------------------------------------------------------------
static void report(as_ota_report_fn fn, void *user, const char *fmt, ...)
{
    if (!fn) return;
    char line[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fn(line, user);
}

void as_ota_allow_downgrade(bool on) { s_allow_downgrade = on; }

bool as_ota_from_url(const char *url, as_ota_report_fn report_fn, void *user, as_ota_result_t *r)
{
    // The permission is for exactly one install: taken here, so neither a refused call nor one already
    // running (which reads its own copy) can carry it over to a later, unforced one.
    const bool allow_downgrade = s_allow_downgrade;
    s_allow_downgrade = false;
    memset(r, 0, sizeof(*r));
    if (!url || (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0)) {
        set_result(r, false, "only http:// and https:// URLs are accepted");
        return false;
    }
    if (__atomic_test_and_set(&s_in_progress, __ATOMIC_ACQUIRE)) {
        set_result(r, false, "another firmware update is already in progress");
        return false;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = HTTP_CHUNK,
        .buffer_size_tx = 1024,
        .disable_auto_redirect = true,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    uint8_t *buf = client ? malloc(HTTP_CHUNK) : NULL;
    bool ok = false;
    int64_t content_length = -1;
    int status = 0;
    sink_t sink = {0};
    if (!client || !buf) {
        set_result(r, false, "http client init failed");
        goto done;
    }
    for (int hop = 0; hop <= MAX_REDIRECTS; ++hop) {
        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            set_result(r, false, "connect failed: %s", esp_err_to_name(err));
            goto done;
        }
        content_length = esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            esp_http_client_set_redirection(client);
            esp_http_client_close(client);
            report(report_fn, user, "Redirect %d, following (%d/%d)", status, hop + 1, MAX_REDIRECTS);
            if (hop == MAX_REDIRECTS) {
                set_result(r, false, "too many redirects");
                goto done;
            }
            continue;
        }
        break;
    }
    if (status != 200) {
        set_result(r, false, "server answered HTTP %d", status);
        goto done;
    }
    if (content_length > 0 && content_length < (int64_t)MIN_IMAGE_BYTES) {
        set_result(r, false, "response too small (%lld B) to be a firmware image", (long long)content_length);
        goto done;
    }
    report(report_fn, user, "Downloading %lld KB", (long long)(content_length > 0 ? content_length / 1024 : -1));
    if (!sink_begin(&sink, r)) goto done;
    sink.allow_downgrade = allow_downgrade;
    report(report_fn, user, "Writing into %s", r->target_label);
    size_t last = 0;
    for (;;) {
        int n = esp_http_client_read(client, (char *)buf, HTTP_CHUNK);
        if (n < 0) {
            set_result(r, false, "download error after %u B", (unsigned)sink.received);
            sink_abort(&sink);
            goto done;
        }
        if (n == 0) {
            if (esp_http_client_is_complete_data_received(client) || content_length <= 0) break;
            set_result(r, false, "connection closed after %u of %lld B", (unsigned)sink.received,
                       (long long)content_length);
            sink_abort(&sink);
            goto done;
        }
        if (!sink_feed(&sink, buf, (size_t)n, r)) {
            sink_abort(&sink);
            goto done;
        }
        if (sink.received - last >= 256 * 1024) {
            last = sink.received;
            if (content_length > 0) {
                report(report_fn, user, "%u KB (%u%%)", (unsigned)(last / 1024), (unsigned)(last * 100 / content_length));
            } else {
                report(report_fn, user, "%u KB", (unsigned)(last / 1024));
            }
        }
    }
    ok = sink_finish(&sink, r);
done:
    free(buf);
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    if (!ok) as_logf("ota: download failed: %s", r->message);
    __atomic_clear(&s_in_progress, __ATOMIC_RELEASE);
    return ok;
}

// ---------------------------------------------------------------------------
// Slots, rollback, self-test
// ---------------------------------------------------------------------------
static const char *state_name(esp_ota_img_states_t st)
{
    switch (st) {
    case ESP_OTA_IMG_NEW: return "new";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending-verify";
    case ESP_OTA_IMG_VALID: return "valid";
    case ESP_OTA_IMG_INVALID: return "invalid";
    case ESP_OTA_IMG_ABORTED: return "aborted";
    default: return "undefined";
    }
}

void as_ota_describe(char *out, size_t cap)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (running) esp_ota_get_state_partition(running, &st);
    snprintf(out, cap, "running %s v%s (%s), next boot %s", running ? running->label : "?",
             as_ota_running_version(), state_name(st), boot ? boot->label : "?");
}

bool as_ota_rollback(char *message, size_t cap)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    if (!running || !other || other->address == running->address) {
        snprintf(message, cap, "no other slot");
        return false;
    }
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(other, &desc) != ESP_OK) {
        snprintf(message, cap, "%s holds no valid image", other->label);
        return false;
    }
    // A valid descriptor is not enough: the other slot may hold the other board's firmware (an install
    // that was refused after the write, in older builds), and it would pass the self-test on Wi-Fi alone.
    if (strncmp(desc.project_name, esp_app_get_description()->project_name, sizeof(desc.project_name)) != 0) {
        snprintf(message, cap, "%s holds an image for '%.32s', not for this board", other->label, desc.project_name);
        return false;
    }
    esp_err_t err = esp_ota_set_boot_partition(other);
    if (err != ESP_OK) {
        snprintf(message, cap, "set boot partition failed: %s", esp_err_to_name(err));
        return false;
    }
    snprintf(message, cap, "next boot from %s (v%s)", other->label, desc.version);
    as_logf("ota: manual rollback to %s v%s", other->label, desc.version);
    return true;
}

void as_ota_note_service_up(void) { s_service_up = true; }
bool as_ota_self_test_pending(void) { return s_self_test_pending; }

static void self_test_task(void *arg)
{
    uint32_t started = now_ms();
    for (;;) {
        if (s_service_up) {
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            as_logf("ota: self-test passed, v%s confirmed (%s)", as_ota_running_version(), esp_err_to_name(err));
            s_self_test_pending = false;
            vTaskDelete(NULL);
            return;
        }
        if (now_ms() - started >= SELF_TEST_TIMEOUT_MS) {
            as_logf("ota: self-test FAILED for v%s, rolling back", as_ota_running_version());
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_ota_mark_app_invalid_rollback_and_reboot();
            // Only reached when there is nothing to roll back to.
            esp_ota_mark_app_valid_cancel_rollback();
            s_self_test_pending = false;
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(SELF_TEST_POLL_MS));
    }
}

void as_ota_self_test_start(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (!running || esp_ota_get_state_partition(running, &st) != ESP_OK || st != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    s_self_test_pending = true;
    as_logf("ota: first boot of v%s in %s, self-test window %u s", as_ota_running_version(), running->label,
            (unsigned)(SELF_TEST_TIMEOUT_MS / 1000));
    if (xTaskCreatePinnedToCore(self_test_task, "ota-selftest", 4096, NULL, 1, NULL, 1) != pdPASS) {
        esp_ota_mark_app_valid_cancel_rollback();
        s_self_test_pending = false;
    }
}

// ---------------------------------------------------------------------------
// Release check
// ---------------------------------------------------------------------------
static bool parse_version(const char *text, int out[3])
{
    if (!text) return false;
    if (*text == 'v' || *text == 'V') ++text;
    out[0] = out[1] = out[2] = 0;
    int part = 0;
    bool digits = false;
    for (; *text; ++text) {
        if (*text >= '0' && *text <= '9') {
            out[part] = out[part] * 10 + (*text - '0');
            digits = true;
        } else if (*text == '.' && part < 2 && digits) {
            ++part;
            digits = false;
        } else {
            break;
        }
    }
    return part >= 1 || digits;
}

static bool is_newer(const char *candidate, const char *current)
{
    int a[3], b[3];
    if (!parse_version(candidate, a) || !parse_version(current, b)) return false;
    for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

static int https_get(const char *url, char *buf, size_t cap, char *error, size_t ecap)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .user_agent = "esp32-air-sound",
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        snprintf(error, ecap, "http client init failed");
        return -1;
    }
    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
    int total = -1;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        snprintf(error, ecap, "connect failed: %s", esp_err_to_name(err));
        goto done;
    }
    esp_http_client_fetch_headers(client);
    {
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            snprintf(error, ecap, "server answered HTTP %d", status);
            goto done;
        }
    }
    total = 0;
    while ((size_t)total < cap - 1) {
        int n = esp_http_client_read(client, buf + total, cap - 1 - total);
        if (n < 0) {
            snprintf(error, ecap, "read error");
            total = -1;
            goto done;
        }
        if (n == 0) break;
        total += n;
    }
    // The buffer is full: one more read says whether the reply fitted exactly (nothing left) or was
    // longer. A reply the buffer could not hold is no reply: cut off, the JSON never parses, and the
    // caller would have retried it hourly for ever without a word about the size.
    if ((size_t)total >= cap - 1) {
        char more;
        int n = esp_http_client_read(client, &more, 1);
        if (n < 0) {
            snprintf(error, ecap, "read error");
            total = -1;
            goto done;
        }
        if (n > 0) {
            snprintf(error, ecap, "response larger than %u KB", (unsigned)(cap / 1024));
            total = -1;
            goto done;
        }
    }
    buf[total] = '\0';
done:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return total;
}

void as_ota_configure_release_check(const char *repo, const char *tag_prefix, const char *asset_name)
{
    if (repo) strncpy(s_repo, repo, sizeof(s_repo) - 1);
    if (tag_prefix) strncpy(s_tag_prefix, tag_prefix, sizeof(s_tag_prefix) - 1);
    if (asset_name) strncpy(s_asset, asset_name, sizeof(s_asset) - 1);
}



bool as_ota_check_for_update(as_ota_update_info_t *out)
{
    if (!s_info_lock) s_info_lock = xSemaphoreCreateMutex();
    as_ota_update_info_t info = {0};
    char *body = heap_caps_malloc(MAX_API_RESPONSE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) body = malloc(MAX_API_RESPONSE);
    if (!body) {
        snprintf(info.error, sizeof(info.error), "out of memory");
    } else {
        char url[160];
        // A release with its assets is about 11 KB of JSON; three of them fit the buffer with room to
        // spare, eight did not (the reply was cut off and never parsed, and the check failed hourly).
        snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases?per_page=3", s_repo);
        int len = https_get(url, body, MAX_API_RESPONSE, info.error, sizeof(info.error));
        if (len > 0) {
            cJSON *root = cJSON_ParseWithLength(body, len);
            if (!root) {
                snprintf(info.error, sizeof(info.error), "release JSON did not parse");
            } else {
                const cJSON *rel = NULL;
                bool found = false;
                char missing[32] = "";   // the newest release with our tag prefix but no asset for this board
                cJSON_ArrayForEach(rel, root) {
                    const cJSON *tag = cJSON_GetObjectItemCaseSensitive(rel, "tag_name");
                    const cJSON *draft = cJSON_GetObjectItemCaseSensitive(rel, "draft");
                    const cJSON *pre = cJSON_GetObjectItemCaseSensitive(rel, "prerelease");
                    if (!cJSON_IsString(tag) || cJSON_IsTrue(draft) || cJSON_IsTrue(pre)) continue;
                    if (strncmp(tag->valuestring, s_tag_prefix, strlen(s_tag_prefix)) != 0) continue;
                    const char *version = tag->valuestring + strlen(s_tag_prefix);
                    const cJSON *assets = cJSON_GetObjectItemCaseSensitive(rel, "assets");
                    const cJSON *asset = NULL;
                    cJSON_ArrayForEach(asset, assets) {
                        const cJSON *name = cJSON_GetObjectItemCaseSensitive(asset, "name");
                        const cJSON *link = cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url");
                        if (cJSON_IsString(name) && cJSON_IsString(link) && strcmp(name->valuestring, s_asset) == 0) {
                            snprintf(info.url, sizeof(info.url), "%s", link->valuestring);
                            break;
                        }
                    }
                    // Both boards share the tag prefix and a release carries one board's image, so the
                    // other board's release is not the end of the list.
                    if (info.url[0] == '\0') {
                        if (!missing[0]) snprintf(missing, sizeof(missing), "%s", version);
                        continue;
                    }
                    snprintf(info.latest_version, sizeof(info.latest_version), "%s", version);
                    found = true;
                    break;
                }
                if (found) {
                    info.checked = true;
                    info.newer = is_newer(info.latest_version, as_ota_running_version());
                } else if (missing[0]) {
                    snprintf(info.error, sizeof(info.error), "release %s has no %s asset", missing, s_asset);
                } else {
                    snprintf(info.error, sizeof(info.error), "no release with tag prefix %s", s_tag_prefix);
                }
                cJSON_Delete(root);
            }
        }
        free(body);
    }
    info.checked_at_ms = now_ms();
    if (info.checked) {
        as_logf("ota: latest release %s is %s than running v%s", info.latest_version,
                info.newer ? "newer" : "not newer", as_ota_running_version());
    } else {
        as_logf("ota: release check failed: %s", info.error);
    }
    xSemaphoreTake(s_info_lock, portMAX_DELAY);
    s_info = info;
    xSemaphoreGive(s_info_lock);
    if (out) *out = info;
    return info.checked;
}

void as_ota_get_update_info(as_ota_update_info_t *out)
{
    if (!s_info_lock) s_info_lock = xSemaphoreCreateMutex();
    xSemaphoreTake(s_info_lock, portMAX_DELAY);
    *out = s_info;
    xSemaphoreGive(s_info_lock);
}

static void checker_report(const char *line, void *user) { as_logf("ota: %s", line); }

static as_ota_may_install_fn s_may_install;

static void checker_task(void *arg)
{
    uint32_t next = now_ms() + FIRST_CHECK_DELAY_MS;
    unsigned fails = 0;
    for (;;) {
        // A 5 s poll for a check that is due once a day woke the chip over 17 000 times a day for
        // nothing. Once the service is up and no self-test is pending, the next check is all there is
        // to wait for: sleep until it, at most half an hour at a time. Until then the poll stays short,
        // so the first check still follows the service coming up within seconds.
        uint32_t delay_ms = 5000;
        if (s_service_up && !s_self_test_pending) {
            int32_t left = (int32_t)(next - now_ms());
            if (left > 5000) delay_ms = left > 30 * 60 * 1000 ? 30U * 60U * 1000U : (uint32_t)left;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        if ((int32_t)(now_ms() - next) < 0 || !s_service_up || s_self_test_pending) continue;
        as_ota_update_info_t info;
        bool ok = as_ota_check_for_update(&info);
        // A failed check is retried after an hour, then two, four... up to the daily interval: a board
        // whose source has nothing for it (no release published yet, a reply it cannot read) must not
        // spend a TLS handshake on the radio every hour of its battery for it.
        fails = ok ? 0 : (fails < 6 ? fails + 1 : fails);
        uint32_t wait = ok ? (uint32_t)CHECK_INTERVAL_MS : (uint32_t)CHECK_RETRY_MS << (fails - 1);
        if (wait > (uint32_t)CHECK_INTERVAL_MS) wait = (uint32_t)CHECK_INTERVAL_MS;
        next = now_ms() + wait;
        if (!ok || !info.newer || !as_config_get()->ota_auto) continue;
        while (s_may_install && !s_may_install()) {
            vTaskDelay(pdMS_TO_TICKS(5 * 60 * 1000));
        }
        as_logf("ota: installing v%s automatically", info.latest_version);
        as_ota_result_t r;
        if (as_ota_from_url(info.url, checker_report, NULL, &r)) {
            as_logf("ota: %s, rebooting", r.message);
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }
    }
}

void as_ota_checker_start(as_ota_may_install_fn may_install)
{
    s_may_install = may_install;
    if (!s_info_lock) s_info_lock = xSemaphoreCreateMutex();
    // The check is a TLS session (to GitHub) and the automatic install runs the whole
    // download on this stack too: the same 8 KB the apps give their install task.
    xTaskCreatePinnedToCore(checker_task, "ota-check", 8192, NULL, 1, NULL, 1);
}


