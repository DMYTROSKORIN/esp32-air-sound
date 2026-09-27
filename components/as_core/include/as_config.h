#pragma once
// Device configuration persisted in NVS (namespace "as"). Every getter returns a default when
// the key is absent, so a freshly flashed board boots into setup mode instead of crashing.
// Descended from smartest-home's sh_config, cut down to what a radio needs.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AS_CFG_STR_MAX 96

typedef struct {
    char wifi_ssid[AS_CFG_STR_MAX];
    char wifi_pass[AS_CFG_STR_MAX];
    char name[33];          // what the phone sees as the AirPlay speaker, also the mDNS hostname base
    char rds_ps[9];         // RDS programme service name, 8 characters, space padded on air
    char rds_rt[65];        // RDS radiotext shown when nothing is playing
    uint16_t freq_10khz;    // FM frequency in 10 kHz units, 7600..10800
    uint16_t rds_pi;        // RDS programme identification
    uint8_t tx_power;       // Si4713 power, 88..115 dBuV
    uint8_t volume;         // 0..100, digital, applied before the DAC
    bool preemph_50us;      // 50 us (Europe) / 75 us pre-emphasis
    bool ota_auto;          // install releases without asking
} as_config_t;

// Loads the configuration from NVS into the global copy. Call once at boot.
void as_config_init(void);
// Read-only pointer to the in-memory configuration.
const as_config_t *as_config_get(void);
// True when Wi-Fi credentials are present.
bool as_config_is_provisioned(void);

// Setters persist immediately and update the in-memory copy.
void as_config_set_str(const char *key, const char *value);
void as_config_set_u8(const char *key, uint8_t value);
void as_config_set_u16(const char *key, uint16_t value);
void as_config_set_bool(const char *key, bool value);
// Erases the namespace (factory reset). The caller reboots.
void as_config_erase(void);

#ifdef __cplusplus
}
#endif
