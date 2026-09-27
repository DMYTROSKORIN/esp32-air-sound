#include "as_config.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "as_config";
static const char *NS = "as";
static as_config_t s_cfg;

#define DEFAULT_NAME "Air-Sound"
#define DEFAULT_PS   "AirSound"
#define DEFAULT_RT   "Air-Sound - esp32-air-sound"

static void load_str(nvs_handle_t h, const char *key, char *out, size_t cap, const char *def)
{
    size_t len = cap;
    esp_err_t err = h ? nvs_get_str(h, key, out, &len) : ESP_ERR_NVS_NOT_FOUND;
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        // Stored by a build with a longer field: keep the start rather than fall back to the default.
        len = 0;
        char *tmp = nvs_get_str(h, key, NULL, &len) == ESP_OK && len > 0 ? malloc(len) : NULL;
        if (tmp && nvs_get_str(h, key, tmp, &len) == ESP_OK) {
            strncpy(out, tmp, cap - 1);
            out[cap - 1] = '\0';
            err = ESP_OK;
        }
        free(tmp);
    }
    if (err != ESP_OK) {
        strncpy(out, def, cap - 1);
        out[cap - 1] = '\0';
    }
}

static uint8_t load_u8(nvs_handle_t h, const char *key, uint8_t def)
{
    uint8_t v = def;
    if (h) nvs_get_u8(h, key, &v);
    return v;
}

static uint16_t load_u16(nvs_handle_t h, const char *key, uint16_t def)
{
    uint16_t v = def;
    if (h) nvs_get_u16(h, key, &v);
    return v;
}

void as_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    memset(&s_cfg, 0, sizeof(s_cfg));
    nvs_handle_t h = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) h = 0;   // no namespace yet: defaults
    load_str(h, "wifi_ssid", s_cfg.wifi_ssid, sizeof(s_cfg.wifi_ssid), "");
    load_str(h, "wifi_pass", s_cfg.wifi_pass, sizeof(s_cfg.wifi_pass), "");
    load_str(h, "name", s_cfg.name, sizeof(s_cfg.name), DEFAULT_NAME);
    load_str(h, "rds_ps", s_cfg.rds_ps, sizeof(s_cfg.rds_ps), DEFAULT_PS);
    load_str(h, "rds_rt", s_cfg.rds_rt, sizeof(s_cfg.rds_rt), DEFAULT_RT);
    s_cfg.freq_10khz = load_u16(h, "freq", 7650);
    s_cfg.rds_pi = load_u16(h, "rds_pi", 0xADAF);
    s_cfg.tx_power = load_u8(h, "tx_power", 105);
    s_cfg.volume = load_u8(h, "volume", 100);
    s_cfg.preemph_50us = load_u8(h, "preemph50", 1) != 0;
    s_cfg.ota_auto = load_u8(h, "ota_auto", 0) != 0;
    if (h) nvs_close(h);

    if (s_cfg.freq_10khz < 7600 || s_cfg.freq_10khz > 10800) s_cfg.freq_10khz = 7650;
    if (s_cfg.tx_power < 88 || s_cfg.tx_power > 115) s_cfg.tx_power = 105;
    if (s_cfg.volume > 100) s_cfg.volume = 100;

    ESP_LOGI(TAG, "name \"%s\", wifi %s, %u.%02u MHz, %u dBuV, RDS \"%s\", ota_auto=%d",
             s_cfg.name, s_cfg.wifi_ssid[0] ? "set" : "(none)", s_cfg.freq_10khz / 100,
             s_cfg.freq_10khz % 100, s_cfg.tx_power, s_cfg.rds_ps, s_cfg.ota_auto);
}

const as_config_t *as_config_get(void) { return &s_cfg; }

bool as_config_is_provisioned(void) { return s_cfg.wifi_ssid[0] != '\0'; }

// The RAM field for a string key and its size; NULL for a key that has no field.
static char *field_for(const char *key, size_t *cap)
{
#define F(k, m) \
    if (!strcmp(key, k)) { \
        *cap = sizeof(s_cfg.m); \
        return s_cfg.m; \
    }
    F("wifi_ssid", wifi_ssid)
    F("wifi_pass", wifi_pass)
    F("name", name)
    F("rds_ps", rds_ps)
    F("rds_rt", rds_rt)
#undef F
    return NULL;
}

void as_config_set_str(const char *key, const char *value)
{
    // NVS gets exactly what RAM holds, so a value that is too long for its field is cut once, here,
    // and reads back the same after a reboot.
    size_t cap = 0;
    char *field = field_for(key, &cap);
    if (field && value != field) {
        size_t n = strlen(value);
        if (n >= cap) ESP_LOGW(TAG, "%s: %u chars, keeping the first %u", key, (unsigned)n, (unsigned)(cap - 1));
        strncpy(field, value, cap - 1);
        field[cap - 1] = '\0';
        value = field;
    }
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        esp_err_t err = nvs_set_str(h, key, value);
        if (err != ESP_OK) ESP_LOGW(TAG, "%s: nvs_set_str %s", key, esp_err_to_name(err));
        nvs_commit(h);
        nvs_close(h);
    }
}

void as_config_set_u8(const char *key, uint8_t value)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, key, value);
        nvs_commit(h);
        nvs_close(h);
    }
    if (!strcmp(key, "tx_power")) s_cfg.tx_power = value;
    else if (!strcmp(key, "volume")) s_cfg.volume = value;
    else if (!strcmp(key, "preemph50")) s_cfg.preemph_50us = value != 0;
    else if (!strcmp(key, "ota_auto")) s_cfg.ota_auto = value != 0;
}

void as_config_set_u16(const char *key, uint16_t value)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u16(h, key, value);
        nvs_commit(h);
        nvs_close(h);
    }
    if (!strcmp(key, "freq")) s_cfg.freq_10khz = value;
    else if (!strcmp(key, "rds_pi")) s_cfg.rds_pi = value;
}

void as_config_set_bool(const char *key, bool value) { as_config_set_u8(key, value ? 1 : 0); }

void as_config_erase(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGW(TAG, "configuration erased");
}
