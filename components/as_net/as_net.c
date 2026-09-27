#include "as_net.h"

#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "as_config.h"
#include "as_log.h"
#include "as_provision.h"
#include <ctype.h>

static as_net_cb_t s_cb;
static void *s_user;
static volatile as_net_state_t s_state = AS_NET_DOWN;
static char s_ip[20] = "0.0.0.0";
static esp_netif_t *s_sta;
static int s_retry;
static bool s_started;
static esp_timer_handle_t s_reconnect;

// Setup-portal trial state. s_trial takes the automatic reconnect out of the picture for good: once
// the owner has started trying networks, nothing else may steer the radio until the device reboots.
// s_trial_armed is narrower - it marks the window in which a disconnect means "this attempt failed"
// rather than "we just dropped the old network on purpose".
#define TRIAL_OK BIT0
#define TRIAL_FAIL BIT1
static volatile bool s_trial;
static volatile bool s_trial_armed;
static EventGroupHandle_t s_trial_ev;
static char s_err[48];

// Power save: MIN_MODEM wakes the radio for every DTIM beacon (about ten times a second) and ignores
// listen_interval; MAX_MODEM sleeps through listen_interval beacons. A screen that is spoken to a few
// times a minute can afford the latency (up to listen_interval x 102 ms for traffic to it).
static bool s_ps_max;
static uint8_t s_listen = 3;
static bool s_radio_off;   // as_net_radio(false): no reconnects until it is switched back on
// The channel the network was last found on. A retry that starts its scan there finds a router that
// is back after one channel instead of thirteen; a router that moved is still found, later in the sweep.
static uint8_t s_channel;

static void reconnect_cb(void *arg)
{
    if (s_radio_off || s_state == AS_NET_PROVISIONING) return;   // the setup portal owns the radio
    wifi_config_t wc;
    if (s_channel && esp_wifi_get_config(WIFI_IF_STA, &wc) == ESP_OK && wc.sta.channel != s_channel) {
        wc.sta.channel = s_channel;
        esp_wifi_set_config(WIFI_IF_STA, &wc);
    }
    esp_wifi_connect();
}

void as_net_set_power_save(bool max_modem, uint8_t listen_interval)
{
    s_ps_max = max_modem;
    s_listen = listen_interval ? listen_interval : 1;
    // The mode takes effect at once; the listen interval is part of the association, so it goes into
    // the station config now and applies from the next association.
    if (s_started) {
        esp_wifi_set_ps(s_ps_max ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM);
        wifi_config_t wc;
        if (esp_wifi_get_config(WIFI_IF_STA, &wc) == ESP_OK && wc.sta.listen_interval != s_listen) {
            wc.sta.listen_interval = s_listen;
            esp_wifi_set_config(WIFI_IF_STA, &wc);
        }
    }
}

void as_net_radio(bool on)
{
    if (!s_started || on == !s_radio_off) return;
    s_radio_off = !on;
    if (on) {
        as_logf("wifi: radio back on");
        esp_wifi_start();   // STA_START connects
    } else {
        as_logf("wifi: radio off");
        if (s_reconnect) esp_timer_stop(s_reconnect);
        esp_wifi_stop();
    }
}


static void set_state(as_net_state_t st)
{
    if (st == s_state) return;
    s_state = st;
    if (s_cb) s_cb(st, s_user);
}

// Disconnect reasons turned into something an owner standing at the device can act on.
static const char *reason_text(int reason)
{
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND: return "network not found";
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "wrong password";
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_ASSOC_TOOMANY: return "the router refused the connection";
        default: return "could not connect";
    }
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    // Setup mode starts the driver in AP+STA mode, so the station raises its own START (and, on a
    // device whose network was down, DISCONNECTED) events. None of them may take the state away from
    // PROVISIONING: the boards' screen, sleep and setup guards key on it, and a connect attempt under
    // the setup AP would drag it across the channels while the owner is on the phone.
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        // During a trial the portal calls connect itself, once the new credentials are in place.
        if (s_trial || s_radio_off || s_state == AS_NET_PROVISIONING) return;
        esp_wifi_connect();
        set_state(AS_NET_CONNECTING);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        if (s_trial) {
            if (s_trial_armed) {
                snprintf(s_err, sizeof(s_err), "%s", reason_text(d->reason));
                as_logf("setup: trial failed, reason %d (%s)", d->reason, s_err);
                if (s_trial_ev) xEventGroupSetBits(s_trial_ev, TRIAL_FAIL);
            }
            return;
        }
        strcpy(s_ip, "0.0.0.0");
        if (s_state == AS_NET_PROVISIONING) return;
        if (s_state == AS_NET_UP) as_logf("wifi: disconnected (reason %d)", d->reason);
        set_state(AS_NET_CONNECTING);
        s_retry++;
        // Back off with a timer: sleeping inside the event loop would stall every other event. Every
        // attempt with the router gone is a sweep of the whole band with the receiver on, so after ten
        // minutes of that the retries settle at one every five minutes: a router that comes back is
        // noticed within that, and a battery lasts.
        int delay_ms = s_retry < 5 ? 1000 : s_retry < 20 ? 5000 : s_retry < 40 ? 30000 : 300000;
        if (s_reconnect && !s_radio_off) esp_timer_start_once(s_reconnect, (uint64_t)delay_ms * 1000);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        if (s_trial) {
            if (s_trial_ev) xEventGroupSetBits(s_trial_ev, TRIAL_OK);
            return;
        }
        if (s_state == AS_NET_PROVISIONING) return;   // a lease renewal does not end setup mode
        s_retry = 0;
        as_logf("wifi: connected to %s, ip %s", as_config_get()->wifi_ssid, s_ip);
        set_state(AS_NET_UP);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        // The event carries the channel; no call back into the driver from its own event handler.
        const wifi_event_sta_connected_t *c = data;
        s_channel = c->channel;
    }
}

void as_net_init(as_net_cb_t cb, void *user)
{
    s_cb = cb;
    s_user = user;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    const esp_timer_create_args_t targs = {.callback = reconnect_cb, .name = "wifi-reconnect"};
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_reconnect));
}

void as_net_start(void)
{
    const as_config_t *c = as_config_get();
    if (!as_config_is_provisioned()) {
        as_logf("wifi: no credentials, opening setup access point");
        as_net_start_provisioning();
        return;
    }
    wifi_config_t wc = {0};
    // An SSID may use all 32 bytes without a terminator; the driver takes the length separately.
    size_t ssid_len = strlen(c->wifi_ssid);
    if (ssid_len > sizeof(wc.sta.ssid)) ssid_len = sizeof(wc.sta.ssid);
    memcpy(wc.sta.ssid, c->wifi_ssid, ssid_len);
    strncpy((char *)wc.sta.password, c->wifi_pass, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = c->wifi_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;
    wc.sta.listen_interval = s_listen;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(s_ps_max ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM);
    esp_netif_set_hostname(s_sta, as_net_hostname());
    s_started = true;
}

void as_net_start_provisioning(void)
{
    if (s_state == AS_NET_PROVISIONING) return;   // a second "Setup mode" press changes nothing
    if (s_reconnect) esp_timer_stop(s_reconnect);   // no reconnect attempts under the setup AP
    set_state(AS_NET_PROVISIONING);
    as_provision_start();
}

as_net_state_t as_net_state(void) { return s_state; }

static void ensure_apsta(void)
{
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK && mode != WIFI_MODE_APSTA) esp_wifi_set_mode(WIFI_MODE_APSTA);
}

int as_net_scan(as_net_ap_t *out, int max)
{
    if (max <= 0) return 0;
    ensure_apsta();
    wifi_scan_config_t sc = {0};
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) return 0;
    uint16_t found = 0;
    esp_wifi_scan_get_ap_num(&found);
    if (found > 40) found = 40;
    if (!found) return 0;
    wifi_ap_record_t *recs = calloc(found, sizeof(*recs));
    if (!recs) return 0;
    esp_wifi_scan_get_ap_records(&found, recs);
    int n = 0;
    for (int i = 0; i < found && n < max; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (!ssid[0]) continue;
        bool seen = false;  // a mesh shows the same name several times; the list wants one row per network
        for (int j = 0; j < n; j++)
            if (strcmp(out[j].ssid, ssid) == 0) seen = true;
        if (seen) continue;
        snprintf(out[n].ssid, sizeof(out[n].ssid), "%s", ssid);
        out[n].rssi = recs[i].rssi;
        out[n].secure = recs[i].authmode != WIFI_AUTH_OPEN;
        n++;
    }
    free(recs);
    return n;
}

bool as_net_try_connect(const char *ssid, const char *pass, uint32_t timeout_ms)
{
    if (!ssid || !ssid[0]) return false;
    if (!s_trial_ev) s_trial_ev = xEventGroupCreate();
    if (!s_trial_ev) return false;
    s_trial = true;
    if (s_reconnect) esp_timer_stop(s_reconnect);
    esp_wifi_disconnect();  // the device may still be on the old network; that drop is not a failure
    vTaskDelay(pdMS_TO_TICKS(200));

    wifi_config_t wc = {0};
    size_t ssid_len = strlen(ssid);
    if (ssid_len > sizeof(wc.sta.ssid)) ssid_len = sizeof(wc.sta.ssid);
    memcpy(wc.sta.ssid, ssid, ssid_len);
    snprintf((char *)wc.sta.password, sizeof(wc.sta.password), "%s", pass ? pass : "");
    // Same threshold the normal start uses, so a network that passes the trial also comes up at boot.
    wc.sta.threshold.authmode = wc.sta.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    ensure_apsta();
    s_err[0] = '\0';
    if (esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK) {
        snprintf(s_err, sizeof(s_err), "%s", "could not use that network name");
        return false;
    }
    xEventGroupClearBits(s_trial_ev, TRIAL_OK | TRIAL_FAIL);
    s_trial_armed = true;
    EventBits_t bits = 0;
    if (esp_wifi_connect() == ESP_OK)
        bits = xEventGroupWaitBits(s_trial_ev, TRIAL_OK | TRIAL_FAIL, pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    s_trial_armed = false;
    if (bits & TRIAL_OK) {
        as_logf("setup: trial connected to %s, ip %s", ssid, s_ip);
        return true;
    }
    esp_wifi_disconnect();
    if (!s_err[0]) snprintf(s_err, sizeof(s_err), "%s", "timed out");
    return false;
}

const char *as_net_last_error(void) { return s_err; }

int as_net_rssi(void)
{
    wifi_ap_record_t ap;
    if (s_state == AS_NET_UP && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
    return 0;
}

const char *as_net_ip(void) { return s_ip; }
const char *as_net_ssid(void) { return as_config_get()->wifi_ssid; }
bool as_net_is_up(void) { return s_state == AS_NET_UP; }

const char *as_net_hostname(void)
{
    static char host[40];
    const char *name = as_config_get()->name;
    size_t o = 0;
    bool dash = false;
    for (const char *p = name; *p && o < sizeof(host) - 1; p++) {
        unsigned char ch = (unsigned char)*p;
        if (isalnum(ch)) {
            host[o++] = (char)tolower(ch);
            dash = false;
        } else if (o && !dash) {
            host[o++] = '-';
            dash = true;
        }
    }
    while (o && host[o - 1] == '-') o--;
    host[o] = '\0';
    if (!o) snprintf(host, sizeof host, "%s", "airsound");
    return host;
}
