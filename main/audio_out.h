#pragma once
// The one path to the DAC. Whoever has audio (the AirPlay receiver, the bench tone) hands 16-bit
// 44.1 kHz stereo frames to a ring in PSRAM; one task drains it into the I2S peripheral as 32-bit
// slots with the gain applied in 32-bit arithmetic, so attenuating for the transmitter's input
// costs no bits of the source. When the ring is empty the task writes silence: the DAC never
// loses its clocks, so there are no clicks when a stream starts or stops.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t audio_out_init(int bck_gpio, int lrck_gpio, int dout_gpio);

// Queues interleaved stereo 16-bit frames. Returns the number of frames taken; the rest are dropped
// (the ring holds about two seconds, which is more than the AirPlay latency).
size_t audio_out_write(const int16_t *frames, size_t nframes);
// Drops everything queued (a seek, a stop).
void audio_out_flush(void);
// Frames waiting in the ring.
size_t audio_out_queued(void);

// Gain in dB relative to full scale, 0 or negative; -144 or lower is mute. Applied on top of the
// build-time line trim. The AirPlay sender's volume lands here.
void audio_out_set_gain_db(float db);
float audio_out_gain_db(void);

// Bench tone (1 kHz, gated) when nothing is queued; off by default.
void audio_out_set_tone(bool on);
// True while frames are flowing from the ring (not silence, not the tone).
bool audio_out_active(void);
