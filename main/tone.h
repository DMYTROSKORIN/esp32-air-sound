#pragma once
// Test tone over I2S to the PCM5102: 44.1 kHz, 16-bit stereo, no MCLK.

#include "esp_err.h"

// Starts a task that streams a sine of tone_hz at level_db below full scale.
esp_err_t tone_start(int bck_gpio, int lrck_gpio, int dout_gpio, int tone_hz, int level_db);
