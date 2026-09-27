#include "airplay.h"

#include <math.h>
#include <stdarg.h>
#include <string.h>

#include "as_config.h"
#include "as_log.h"
#include "audio_out.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "fm.h"
#include "raop.h"
#include "esp_heap_caps.h"

// Frame buffer for the RTP layer: room for its maximum of ten seconds at 352 frames a packet,
// in PSRAM. Left to its own devices it would malloc small blocks that land in internal RAM.
#define RTP_BUF_BYTES (1252 * 1408)
static uint8_t *s_rtp_buf;

static const char *TAG = "airplay";
static struct raop_ctx_s *s_raop;
static airplay_state_cb_t s_cb;
static volatile bool s_streaming;
static char s_artist[64], s_title[96];

static void set_streaming(bool on)
{
    if (s_streaming == on) return;
    s_streaming = on;
    if (s_cb) s_cb(on);
}

// The receiver's events, in the order a session produces them: SETUP, STREAM, then VOLUME,
// METADATA, PROGRESS, FLUSH on seeks, and STOP (or STALLED when the sender vanished).
static bool cmd_cb(raop_event_t event, ...)
{
    va_list ap;
    va_start(ap, event);
    switch (event) {
    case RAOP_SETUP: {
        // Optional buffer for the RTP layer; letting it allocate its own (in PSRAM) is fine.
        uint8_t **buf = va_arg(ap, uint8_t **);
        size_t *size = va_arg(ap, size_t *);
        if (!s_rtp_buf) s_rtp_buf = heap_caps_malloc(RTP_BUF_BYTES, MALLOC_CAP_SPIRAM);
        *buf = s_rtp_buf;
        *size = s_rtp_buf ? RTP_BUF_BYTES : 0;
        audio_out_flush();
        as_logf("airplay: session setup");
        break;
    }
    case RAOP_STREAM:
    case RAOP_PLAY:
    case RAOP_RESUME:
        set_streaming(true);
        break;
    case RAOP_FLUSH:
        audio_out_flush();
        break;
    case RAOP_PAUSE:
        break;
    case RAOP_STOP:
    case RAOP_STALLED:
        audio_out_flush();
        set_streaming(false);
        fm_clear_now_playing();
        s_artist[0] = s_title[0] = '\0';
        as_logf("airplay: session %s", event == RAOP_STOP ? "ended" : "stalled");
        break;
    case RAOP_VOLUME: {
        // raop.c hands over 0 for mute or 1 + dB/30 for the sender's -30..0 dB range.
        double v = va_arg(ap, double);
        float db = v <= 0.0 ? -144.0f : (float)((v - 1.0) * 30.0);
        audio_out_set_gain_db(db);
        ESP_LOGI(TAG, "volume %.1f dB", db);
        break;
    }
    case RAOP_METADATA: {
        char *artist = va_arg(ap, char *);
        char *album = va_arg(ap, char *);
        char *title = va_arg(ap, char *);
        (void)album;
        snprintf(s_artist, sizeof s_artist, "%s", artist ? artist : "");
        snprintf(s_title, sizeof s_title, "%s", title ? title : "");
        as_logf("airplay: now playing %s - %s", s_artist, s_title);
        fm_set_now_playing(s_artist, s_title);
        break;
    }
    case RAOP_ARTWORK:
    case RAOP_PROGRESS:
    case RAOP_TIMING:
    default:
        break;
    }
    va_end(ap);
    return true;
}

// Decoded 16-bit stereo PCM, due at `playtime`; the RTP layer already waited for that moment.
static void data_cb(const u8_t *data, size_t len, u32_t playtime)
{
    (void)playtime;
    size_t frames = len / 4;
    size_t taken = audio_out_write((const int16_t *)data, frames);
    if (taken < frames) ESP_LOGW(TAG, "dropped %u frames", (unsigned)(frames - taken));
}

void airplay_init(airplay_state_cb_t cb) { s_cb = cb; }

void airplay_start(void)
{
    if (s_raop) return;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {0};
    if (!netif || esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0) {
        ESP_LOGW(TAG, "no station address yet");
        return;
    }
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    const as_config_t *c = as_config_get();
    static char name[33];
    snprintf(name, sizeof name, "%s", c->name);
    // Latency 0 lets the sender choose (about two seconds for iOS, less for macOS/PipeWire).
    s_raop = raop_create(ip.ip.addr, name, mac, 0, cmd_cb, data_cb);
    if (s_raop) as_logf("airplay: \"%s\" advertised", name);
    else as_logf("airplay: failed to start");
}

bool airplay_streaming(void) { return s_streaming; }

void airplay_toggle(void) { if (s_raop) raop_cmd(s_raop, RAOP_TOGGLE, NULL); }
void airplay_next(void) { if (s_raop) raop_cmd(s_raop, RAOP_NEXT, NULL); }
void airplay_prev(void) { if (s_raop) raop_cmd(s_raop, RAOP_PREV, NULL); }
