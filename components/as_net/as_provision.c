// Setup access point: open SSID AS_SETUP_SSID, DHCP, a tiny DNS server that answers every name
// with our own address (captive portal), and one HTML form.
//
// Descended from smartest-home's setup page and esp32-s3-n16r8-bastion's portal, with the hub
// pairing taken out and the radio's own settings put in. The rule that survived both: nothing is
// written until the device has actually joined the network the owner typed, with the setup
// access point still up, so a wrong password comes back as a sentence on the phone instead of a
// device that silently never appears.

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "as_config.h"
#include "as_log.h"
#include "as_net.h"
#include "as_provision.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "as_provision";
static httpd_handle_t s_httpd;
static esp_timer_handle_t s_setup_timer;   // an already provisioned device leaves setup mode on its own
static bool s_started;

#define SETUP_TIMEOUT_US (15ULL * 60 * 1000000)
#define SCAN_MAX 16
#define TRIAL_TIMEOUT_MS 20000

typedef enum { ST_IDLE, ST_WORKING, ST_FAILED, ST_DONE } state_t;

static as_net_ap_t s_aps[SCAN_MAX];
static int s_ap_count;
static volatile state_t s_state;
static char s_msg[160];
static as_provision_status_cb_t s_status_cb;

// What "Apply" submitted, handed to the worker task. One setup at a time, so one slot is enough.
typedef struct {
    char ssid[64];
    char pass[AS_CFG_STR_MAX];
    bool keep_pass;      // the owner left the password field empty on a provisioned device: keep the old one
    char name[33];
    char rds_ps[9];
    uint16_t freq_10khz;
    uint8_t tx_power;
    bool ota_auto;
} pending_t;
static pending_t s_pending;

static void set_status(state_t st, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void set_status(state_t st, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_msg, sizeof(s_msg), fmt, ap);
    va_end(ap);
    s_state = st;
    as_logf("setup: %s", s_msg);
    if (s_status_cb) s_status_cb(s_msg, st == ST_DONE, st == ST_FAILED);
}

void as_provision_set_status_cb(as_provision_status_cb_t cb) { s_status_cb = cb; }

static void url_decode(char *s)
{
    char *o = s;
    for (; *s; ++s, ++o) {
        if (*s == '+') {
            *o = ' ';
        } else if (*s == '%' && s[1] && s[2]) {
            char hex[3] = {s[1], s[2], 0};
            *o = (char)strtol(hex, NULL, 16);
            s += 2;
        } else {
            *o = *s;
        }
    }
    *o = '\0';
}

static bool form_get(const char *body, const char *key, char *out, size_t cap)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "%s=", key);
    const char *p = body;
    while ((p = strstr(p, pat)) != NULL) {
        if (p == body || p[-1] == '&') {
            p += strlen(pat);
            const char *e = strchr(p, '&');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            if (n >= cap) n = cap - 1;
            memcpy(out, p, n);
            out[n] = '\0';
            url_decode(out);
            return true;
        }
        p += strlen(pat);
    }
    out[0] = '\0';
    return false;
}

// Escapes into the caller's buffer. Network names come from the air, so they are not trusted.
static void html_attr(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    for (; *in && o + 7 < cap; ++in) {
        if (*in == '\'' || *in == '"' || *in == '<' || *in == '>' || *in == '&') {
            o += snprintf(out + o, cap - o, "&#%d;", (unsigned char)*in);
        } else {
            out[o++] = *in;
        }
    }
    out[o] = '\0';
}

static const char k_head[] =
    "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Air Sound setup</title><style>"
    ":root{--bg:#0f172a;--card:#1e293b;--line:#334155;--text:#e2e8f0;--muted:#94a3b8;--accent:#38bdf8;--err:#fb7185}"
    "body{font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);margin:0;padding:20px 16px 40px}"
    "h1{font-size:22px;margin:0 0 4px}p.sub{color:var(--muted);margin:0 0 18px;font-size:14px}"
    "form{max-width:440px}label{display:block;margin:14px 0 6px;font-size:14px;color:var(--muted)}"
    "input,select{width:100%%;box-sizing:border-box;padding:12px;border-radius:10px;border:1px solid var(--line);"
    "background:var(--card);color:#fff;font-size:16px}input:focus,select:focus{outline:none;border-color:var(--accent)}"
    ".row{display:flex;gap:10px}.row>div{flex:1}"
    "button{margin-top:22px;width:100%%;padding:14px;border:0;border-radius:12px;background:var(--accent);color:#0f172a;"
    "font-weight:700;font-size:16px}button.sec{background:var(--card);color:var(--text);margin-top:8px;padding:10px}"
    "a{color:var(--accent)}small{color:var(--muted);display:block;margin-top:4px;font-size:12px}"
    ".chk{display:flex;align-items:center;gap:10px;margin-top:18px;font-size:15px;color:var(--text)}"
    ".chk input{width:22px;height:22px;margin:0}"
    ".msg{max-width:440px;padding:12px;border-radius:10px;background:var(--card);margin-top:16px;color:var(--muted)}"
    "</style></head><body><h1>Air Sound</h1><p class='sub'>Firmware %s. This page sets up the radio.</p>";

static const char k_form_top[] =
    "<form method='POST' action='/apply'>"
    "<label for='ssid'>Wi-Fi network</label><select id='ssid' name='wifi_ssid' onchange=\"document.getElementById('m').style.display=this.value==''?'block':'none'\">";

// Arguments: manual-ssid value, password placeholder, name, rds_ps, freq MHz (x.xx), power, ota checked.
static const char k_form_bottom[] =
    "<option value=''>Other network (type its name)</option></select>"
    "<input id='m' name='wifi_ssid_manual' placeholder='Network name (SSID)' autocapitalize='none' autocorrect='off' "
    "style='display:none;margin-top:8px' value='%s'>"
    "<button type='button' class='sec' onclick=\"location.href='/scan'\">Rescan networks</button>"
    "<label for='pass'>Wi-Fi password</label><input id='pass' name='wifi_pass' type='password' autocomplete='off' placeholder='%s'>"
    "<small>2.4 GHz networks only.</small>"
    "<label for='name'>Station name</label><input id='name' name='name' maxlength='32' value='%s' placeholder='Air-Sound'>"
    "<small>What phones and laptops see as the audio output.</small>"
    "<label for='ps'>RDS name (8 characters)</label><input id='ps' name='rds_ps' maxlength='8' value='%s' placeholder='Air-Sound' style='text-transform:uppercase'>"
    "<div class='row'><div><label for='f'>Frequency, MHz</label><input id='f' name='freq' inputmode='decimal' value='%s'>"
    "<small>76.00 to 108.00, step 0.05</small></div>"
    "<div><label for='pw'>Power, dB&micro;V</label><input id='pw' name='tx_power' inputmode='numeric' value='%u'>"
    "<small>88 to 115</small></div></div>"
    "<label class='chk'><input type='checkbox' name='ota_auto' value='1' %s>Install firmware updates automatically</label>"
    "<button type='submit'>Apply &amp; restart</button></form>"
    "<div class='msg' id='msg'>%s</div>"
    "</body></html>";

static size_t append(char *buf, size_t cap, size_t n, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

static size_t append(char *buf, size_t cap, size_t n, const char *fmt, ...)
{
    if (n >= cap) return n;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + n, cap - n, fmt, ap);
    va_end(ap);
    if (w < 0) return n;
    return n + (size_t)w >= cap ? cap - 1 : n + (size_t)w;
}

static esp_err_t page_handler(httpd_req_t *req)
{
    const size_t cap = 8192;
    const size_t esc_cap = sizeof(s_aps[0].ssid) * 7;
    char *page = malloc(cap);
    char *esc = malloc(esc_cap);
    if (!page || !esc) {
        free(page);
        free(esc);
        return httpd_resp_send_500(req);
    }
    const as_config_t *c = as_config_get();
    size_t n = 0;
    n = append(page, cap, n, k_head, esp_app_get_description()->version);
    n = append(page, cap, n, "%s", k_form_top);
    bool current_listed = false;
    for (int i = 0; i < s_ap_count && n + 2 * esc_cap + 96 + sizeof(k_form_bottom) + 400 < cap; i++) {
        html_attr(s_aps[i].ssid, esc, esc_cap);
        bool sel = strcmp(s_aps[i].ssid, c->wifi_ssid) == 0;
        current_listed |= sel;
        n = append(page, cap, n, "<option value='%s'%s>%s (%d dBm)%s</option>", esc, sel ? " selected" : "", esc,
                   s_aps[i].rssi, s_aps[i].secure ? "" : " open");
    }
    if (!s_ap_count) n = append(page, cap, n, "<option value=''>no networks found - rescan</option>");
    // Prefilled with the current values, except the password: its field says a value is kept.
    char manual[64 * 7], name[33 * 7], ps[9 * 7], freq[16];
    html_attr(current_listed ? "" : c->wifi_ssid, manual, sizeof manual);
    html_attr(c->name, name, sizeof name);
    html_attr(c->rds_ps, ps, sizeof ps);
    snprintf(freq, sizeof freq, "%u.%02u", c->freq_10khz / 100, c->freq_10khz % 100);
    n = append(page, cap, n, k_form_bottom, manual,
               c->wifi_pass[0] ? "saved - leave empty to keep it" : "Leave blank for open networks", name, ps, freq,
               c->tx_power, c->ota_auto ? "checked" : "", s_msg);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
    free(page);
    free(esc);
    return ESP_OK;
}

static esp_err_t scan_handler(httpd_req_t *req)
{
    s_ap_count = as_net_scan(s_aps, SCAN_MAX);
    as_logf("setup: %d networks in range", s_ap_count);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static void apply_task(void *arg)
{
    const as_config_t *c = as_config_get();
    const char *pass = s_pending.keep_pass ? c->wifi_pass : s_pending.pass;
    set_status(ST_WORKING, "joining %s", s_pending.ssid);
    if (!as_net_try_connect(s_pending.ssid, pass, TRIAL_TIMEOUT_MS)) {
        set_status(ST_FAILED, "%s: %s", s_pending.ssid, as_net_last_error());
        vTaskDelete(NULL);
        return;
    }
    // Nothing is written until here, so a failed attempt leaves a working device untouched.
    as_config_set_str("wifi_ssid", s_pending.ssid);
    if (!s_pending.keep_pass) as_config_set_str("wifi_pass", s_pending.pass);
    as_config_set_str("name", s_pending.name);
    as_config_set_str("rds_ps", s_pending.rds_ps);
    as_config_set_u16("freq", s_pending.freq_10khz);
    as_config_set_u8("tx_power", s_pending.tx_power);
    as_config_set_bool("ota_auto", s_pending.ota_auto);
    set_status(ST_DONE, "connected to %s as \"%s\", %u.%02u MHz. Restarting.", s_pending.ssid, s_pending.name,
               s_pending.freq_10khz / 100, s_pending.freq_10khz % 100);
    vTaskDelay(pdMS_TO_TICKS(4000));
    esp_restart();
}

static void restart_setup_window(void)
{
    if (!s_setup_timer) return;
    esp_timer_stop(s_setup_timer);
    esp_timer_start_once(s_setup_timer, SETUP_TIMEOUT_US);
}

// "76.5", "76,50", "7650" all mean 76.50 MHz. Returns 0 when it is not a frequency the chip can do.
static uint16_t parse_freq(const char *s)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%s", s);
    for (char *p = buf; *p; p++)
        if (*p == ',') *p = '.';
    double mhz = atof(buf);
    if (mhz > 1000) mhz /= 100;   // typed in 10 kHz units
    long v = (long)(mhz * 100 + 0.5);
    if (v < 7600 || v > 10800 || v % 5) return 0;
    return (uint16_t)v;
}

static esp_err_t apply_handler(httpd_req_t *req)
{
    char *body = malloc(1024);
    pending_t *p = calloc(1, sizeof(*p));
    char field[64];
    if (!body || !p) {
        free(body);
        free(p);
        return httpd_resp_send_500(req);
    }
    int total = 0;
    while (total < 1023 && total < req->content_len) {
        int n = httpd_req_recv(req, body + total, 1023 - total);
        if (n <= 0) break;
        total += n;
    }
    body[total] = '\0';
    form_get(body, "wifi_ssid", p->ssid, sizeof(p->ssid));
    if (!p->ssid[0]) form_get(body, "wifi_ssid_manual", p->ssid, sizeof(p->ssid));
    form_get(body, "wifi_pass", p->pass, sizeof(p->pass));
    form_get(body, "name", p->name, sizeof(p->name));
    form_get(body, "rds_ps", p->rds_ps, sizeof(p->rds_ps));
    form_get(body, "freq", field, sizeof field);
    p->freq_10khz = parse_freq(field);
    form_get(body, "tx_power", field, sizeof field);
    long power = atol(field);
    p->ota_auto = form_get(body, "ota_auto", field, sizeof field) && field[0] == '1';
    free(body);

    for (char *q = p->rds_ps; *q; q++) *q = (char)toupper((unsigned char)*q);
    if (!p->name[0]) snprintf(p->name, sizeof p->name, "%s", "Air-Sound");
    if (!p->rds_ps[0]) snprintf(p->rds_ps, sizeof p->rds_ps, "%.8s", p->name);
    const as_config_t *c = as_config_get();
    // An empty password on a provisioned device means "keep the saved one" for the same network.
    p->keep_pass = !p->pass[0] && c->wifi_pass[0] && strcmp(p->ssid, c->wifi_ssid) == 0;

    if (s_state == ST_WORKING || s_state == ST_DONE) {
        free(p);   // an attempt in flight keeps its state; the page below polls it
    } else {
        const char *reject = NULL;
        if (!p->ssid[0]) reject = "pick a network first";
        else if (!p->freq_10khz) reject = "frequency must be 76.00 to 108.00 MHz in 0.05 steps";
        else if (power < 88 || power > 115) reject = "power must be 88 to 115";
        if (reject) {
            set_status(ST_FAILED, "%s", reject);
        } else {
            p->tx_power = (uint8_t)power;
            s_pending = *p;
            set_status(ST_WORKING, "connecting...");
            if (xTaskCreatePinnedToCore(apply_task, "as-setup", 6144, NULL, 4, NULL, 0) != pdPASS)
                set_status(ST_FAILED, "out of memory, restart the device");
            else
                restart_setup_window();
        }
        free(p);
    }
    // Joining the owner's network moves the access point to that network's channel, which may drop
    // the phone off it for a moment. The page polls and says so.
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
                    "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<body style='font-family:system-ui,sans-serif;background:#0f172a;color:#e2e8f0;padding:24px'>"
                    "<h2 id='s'>Connecting...</h2><p id='h' style='color:#94a3b8'>This can take half a minute. If this page "
                    "stops updating, the device has moved to your network: watch its LED. Steady breathing blue means it is "
                    "connecting, two green flashes mean it is on air.</p>"
                    "<p><a href='/' style='color:#38bdf8'>Back to the form</a></p>"
                    "<script>setInterval(async()=>{try{const r=await fetch('/status');const j=await r.json();"
                    "document.getElementById('s').textContent=j.msg;"
                    "if(j.state=='done')document.getElementById('h').textContent='All set. The device is restarting.';}"
                    "catch(e){}},1500)</script></body>",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    const char *st = s_state == ST_DONE ? "done" : s_state == ST_FAILED ? "failed" : s_state == ST_WORKING ? "working" : "idle";
    const size_t cap = sizeof(s_msg) * 2 + 64;
    char *esc = malloc(cap);
    char *buf = malloc(cap + 64);
    if (!esc || !buf) {
        free(esc);
        free(buf);
        return httpd_resp_send_500(req);
    }
    size_t o = 0;
    for (const char *p = s_msg; *p && o + 3 < cap; ++p) {
        if (*p == '\n') {
            esc[o++] = '\\';
            esc[o++] = 'n';
            continue;
        }
        if (*p == '"' || *p == '\\') esc[o++] = '\\';
        esc[o++] = (*p < 0x20) ? ' ' : *p;
    }
    esc[o] = '\0';
    snprintf(buf, cap + 64, "{\"state\":\"%s\",\"msg\":\"%s\"}", st, esc);
    free(esc);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    free(buf);
    return ESP_OK;
}

static esp_err_t redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static void setup_timeout_cb(void *arg)
{
    if (s_state == ST_DONE) return;
    if (s_state == ST_WORKING) {
        if (s_setup_timer) esp_timer_start_once(s_setup_timer, 60ULL * 1000000);
        return;
    }
    as_logf("setup: no changes within 15 min, leaving setup mode");
    esp_restart();
}

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "dns socket failed");
        vTaskDelete(NULL);
        return;
    }
    uint8_t buf[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n < 12) continue;
        // Answer every A query with 192.168.4.1.
        buf[2] = 0x81; buf[3] = 0x80;
        buf[6] = 0; buf[7] = 1;
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        int q = 12;
        while (q < n && buf[q]) q += buf[q] + 1;
        q += 5;
        if (q > n || q + 16 > (int)sizeof(buf)) continue;
        uint8_t ans[16] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1};
        memcpy(buf + q, ans, sizeof(ans));
        sendto(sock, buf, q + sizeof(ans), 0, (struct sockaddr *)&from, fl);
    }
}

static void scan_task(void *arg)
{
    s_ap_count = as_net_scan(s_aps, SCAN_MAX);
    as_logf("setup: %d networks in range", s_ap_count);
    vTaskDelete(NULL);
}

void as_provision_start(void)
{
    if (s_started) {
        ESP_LOGW(TAG, "setup AP already up");
        return;
    }
    s_started = true;
    esp_netif_create_default_wifi_ap();
    wifi_config_t ap = {0};
    strcpy((char *)ap.ap.ssid, AS_SETUP_SSID);
    ap.ap.ssid_len = strlen(AS_SETUP_SSID);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 6144;
    if (httpd_start(&s_httpd, &cfg) == ESP_OK) {
        httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = page_handler};
        httpd_uri_t scan = {.uri = "/scan", .method = HTTP_GET, .handler = scan_handler};
        httpd_uri_t status = {.uri = "/status", .method = HTTP_GET, .handler = status_handler};
        httpd_uri_t apply = {.uri = "/apply", .method = HTTP_POST, .handler = apply_handler};
        httpd_uri_t any = {.uri = "/*", .method = HTTP_GET, .handler = redirect_handler};
        httpd_register_uri_handler(s_httpd, &root);
        httpd_register_uri_handler(s_httpd, &scan);
        httpd_register_uri_handler(s_httpd, &status);
        httpd_register_uri_handler(s_httpd, &apply);
        httpd_register_uri_handler(s_httpd, &any);
    }
    xTaskCreatePinnedToCore(dns_task, "captive-dns", 3072, NULL, 3, NULL, 0);
    // Scan before anyone joins: it hops channels, which would disturb a connected phone.
    xTaskCreatePinnedToCore(scan_task, "setup-scan", 3072, NULL, 3, NULL, 0);
    if (as_config_is_provisioned()) {
        const esp_timer_create_args_t targs = {.callback = setup_timeout_cb, .name = "setup-timeout"};
        if (esp_timer_create(&targs, &s_setup_timer) != ESP_OK) s_setup_timer = NULL;
        restart_setup_window();
    }
    snprintf(s_msg, sizeof(s_msg), "Join the Wi-Fi network %s and this page opens; otherwise browse to 192.168.4.1.",
             AS_SETUP_SSID);
    ESP_LOGI(TAG, "setup AP up: %s / http://192.168.4.1", AS_SETUP_SSID);
}
