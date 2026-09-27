// CPU load, memory and chip temperature with a short history for a sparkline.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AS_SYSMON_POINTS 60   // 5 minutes at one sample per 5 s

typedef struct {
    uint8_t cpu_pct;          // both cores
    uint8_t cpu_core[2];
    int16_t temp_c;           // INT16_MIN when the sensor is unavailable
    // internal_free and internal_min sum every internal heap, including the pool that
    // CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL sets aside for DMA and task stacks - memory a plain
    // malloc() (cJSON, strdup, the MQTT client's buffers) can never get. internal_usable and its
    // minimum are what malloc() can actually draw on inside internal RAM; internal_largest is the
    // biggest block a task stack could take. The hub's history keeps the old two; the new ones are
    // added beside them.
    uint32_t internal_free, internal_min, psram_free;
    uint32_t internal_usable, internal_usable_min, internal_largest;
    uint8_t count;            // samples in the history, up to AS_SYSMON_POINTS
} as_sysmon_t;

void as_sysmon_init(void);
// Takes one sample; call every 5 s from a normal task.
void as_sysmon_sample(void);
const as_sysmon_t *as_sysmon_get(void);
// History oldest first, missing points as `none`.
void as_sysmon_history(int32_t cpu[AS_SYSMON_POINTS], int32_t ram_kb[AS_SYSMON_POINTS], int32_t none);

// How much of the time the chip actually slept, which is the one number that decides a battery's life
// and the one nothing else here reports: CPU load reads near zero whether the chip is idling at full
// speed or switched off. Measured from the light-sleep callbacks, so it costs nothing to keep.
//
// `as_sysmon_sleep_window` gives the share and the wake rate since the previous call and starts a new
// window - for telemetry, so the hub stores a rate rather than a total that only ever grows.
// `as_sysmon_sleep_total` gives the seconds slept and the number of wake-ups since boot, and changes
// nothing - for a log line. Both return false in a build with no automatic light sleep to measure.
bool as_sysmon_sleep_window(uint8_t *pct, uint16_t *wakes_min);
bool as_sysmon_sleep_total(uint32_t *slept_s, uint32_t *wakes);

#ifdef __cplusplus
}
#endif
