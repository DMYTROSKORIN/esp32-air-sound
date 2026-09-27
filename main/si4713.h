#pragma once
// Si4713 FM transmitter over I2C, transmit function only. Command set from Silicon Labs
// AN332; the bring-up order follows what is known to work on the Adafruit breakout, which
// the CJMCU-4713 copies.

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t addr;
    int rst_gpio;
} si4713_t;

typedef struct {
    uint8_t part_number;   // 13 for the Si4713
    uint8_t fw_major, fw_minor;
    uint8_t patch_hi, patch_lo;
    uint8_t cmp_major, cmp_minor;
    uint8_t chip_rev;
} si4713_rev_t;

typedef struct {
    uint16_t freq_10khz;   // e.g. 10000 = 100.00 MHz
    uint8_t power_dbuv;
    uint8_t antcap;        // antenna tuning capacitor, 0.25 pF steps
    uint8_t noise_level;   // received noise level at that frequency, dBuV
} si4713_tune_status_t;

typedef struct {
    bool overmod;          // audio hit the limiter
    bool level_high, level_low;
    int8_t in_level_db;    // input level, dBfs
} si4713_asq_t;

// Pulses RST, adds the device to the bus, powers the chip up in analog-input transmit
// mode and reads its revision. Returns ESP_ERR_NOT_FOUND when nothing answers at addr.
esp_err_t si4713_init(si4713_t *tx, i2c_master_bus_handle_t bus, uint8_t addr, int rst_gpio,
                      si4713_rev_t *rev);

esp_err_t si4713_set_property(si4713_t *tx, uint16_t prop, uint16_t value);
esp_err_t si4713_get_property(si4713_t *tx, uint16_t prop, uint16_t *value);

// Pre-emphasis, line input level, stereo pilot + RDS components. Call once after init.
esp_err_t si4713_configure_audio(si4713_t *tx, bool preemph_50us);

esp_err_t si4713_tune_power(si4713_t *tx, uint8_t power_dbuv, uint8_t antcap);
esp_err_t si4713_tune_freq(si4713_t *tx, uint16_t freq_10khz);
esp_err_t si4713_tune_status(si4713_t *tx, si4713_tune_status_t *st);
esp_err_t si4713_asq_status(si4713_t *tx, si4713_asq_t *asq);

// RDS: programme identification, 8-character PS name, radiotext up to 64 characters.
esp_err_t si4713_rds_begin(si4713_t *tx, uint16_t pi);
esp_err_t si4713_rds_set_ps(si4713_t *tx, const char *ps8);
esp_err_t si4713_rds_set_radiotext(si4713_t *tx, const char *text);
