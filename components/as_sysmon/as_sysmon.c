#include "as_sysmon.h"

#include "driver/temperature_sensor.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static as_sysmon_t s;
static uint8_t s_cpu_hist[AS_SYSMON_POINTS];
static uint16_t s_ram_hist[AS_SYSMON_POINTS];
static uint8_t s_head;
static uint32_t s_last_us, s_last_idle[2];
static temperature_sensor_handle_t s_tsens;

// ---------------------------------------------------------------------------
// Light sleep, counted rather than assumed.
//
// The frequency tally in `esp_pm_dump_locks` is a wall of text meant for a human at a serial port. A
// battery question needs one number over a window, and needs it where the history is - in the hub -
// so the device answers "how much of the last five minutes was I switched off" with every telemetry
// message. The IDF hands the sleep duration to the exit callback, so this is arithmetic in the idle
// task and nothing more.
// ---------------------------------------------------------------------------
#if CONFIG_PM_ENABLE && CONFIG_FREERTOS_USE_TICKLESS_IDLE && CONFIG_PM_LIGHT_SLEEP_CALLBACKS
static portMUX_TYPE s_sleep_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_sleep_us, s_win_sleep_us, s_win_start_us;
static uint32_t s_wakes, s_win_wakes;

// Runs in the idle task straight out of sleep; it may not block, and it must be in RAM because the
// flash cache is not necessarily back yet.
static IRAM_ATTR esp_err_t sleep_exit_cb(int64_t slept_us, void *arg)
{
    (void)arg;
    portENTER_CRITICAL_ISR(&s_sleep_mux);
    s_sleep_us += slept_us;
    s_win_sleep_us += slept_us;
    s_wakes++;
    s_win_wakes++;
    portEXIT_CRITICAL_ISR(&s_sleep_mux);
    return ESP_OK;
}

static void sleep_accounting_init(void)
{
    s_win_start_us = esp_timer_get_time();
    esp_pm_sleep_cbs_register_config_t cbs = {.exit_cb = sleep_exit_cb, .exit_cb_prior = 100};
    esp_pm_light_sleep_register_cbs(&cbs);
}

bool as_sysmon_sleep_window(uint8_t *pct, uint16_t *wakes_min)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_sleep_mux);
    int64_t slept = s_win_sleep_us, wall = now - s_win_start_us;
    uint32_t wakes = s_win_wakes;
    s_win_sleep_us = 0;
    s_win_wakes = 0;
    s_win_start_us = now;
    portEXIT_CRITICAL(&s_sleep_mux);
    if (wall <= 0) return false;
    if (slept > wall) slept = wall;   // a window that began inside a sleep
    if (pct) *pct = (uint8_t)(slept * 100 / wall);
    // Capped rather than wrapped: a rate this high is already the answer, and the exact figure past
    // it would not change what anyone does about it.
    uint64_t per_min = (uint64_t)wakes * 60000000ULL / (uint64_t)wall;
    if (wakes_min) *wakes_min = per_min > 65000 ? 65000 : (uint16_t)per_min;
    return true;
}

bool as_sysmon_sleep_total(uint32_t *slept_s, uint32_t *wakes)
{
    portENTER_CRITICAL(&s_sleep_mux);
    int64_t slept = s_sleep_us;
    uint32_t n = s_wakes;
    portEXIT_CRITICAL(&s_sleep_mux);
    if (slept_s) *slept_s = (uint32_t)(slept / 1000000);
    if (wakes) *wakes = n;
    return true;
}
#else
static void sleep_accounting_init(void) {}

bool as_sysmon_sleep_window(uint8_t *pct, uint16_t *wakes_min)
{
    (void)pct;
    (void)wakes_min;
    return false;
}

bool as_sysmon_sleep_total(uint32_t *slept_s, uint32_t *wakes)
{
    (void)slept_s;
    (void)wakes;
    return false;
}
#endif

void as_sysmon_init(void)
{
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&cfg, &s_tsens) != ESP_OK || temperature_sensor_enable(s_tsens) != ESP_OK) s_tsens = NULL;
    s.temp_c = INT16_MIN;
    s_last_us = (uint32_t)esp_timer_get_time();
    for (int c = 0; c < 2; c++) s_last_idle[c] = ulTaskGetIdleRunTimeCounterForCore(c);
    sleep_accounting_init();
}

void as_sysmon_sample(void)
{
    // Run-time counters are microseconds (uint32, wraps every 71 min); unsigned deltas stay correct across the wrap.
    uint32_t now = (uint32_t)esp_timer_get_time();
    uint32_t elapsed = now - s_last_us;
    s_last_us = now;
    unsigned busy = 0;
    for (int c = 0; c < 2; c++) {
        uint32_t idle = ulTaskGetIdleRunTimeCounterForCore(c);
        uint32_t d = idle - s_last_idle[c];
        s_last_idle[c] = idle;
        unsigned load = elapsed && d < elapsed ? 100 - (unsigned)((uint64_t)d * 100 / elapsed) : 0;
        s.cpu_core[c] = (uint8_t)load;
        busy += load;
    }
    s.cpu_pct = (uint8_t)(busy / 2);
    s.internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s.internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    // The reserved pool is registered without MALLOC_CAP_DEFAULT, so asking for DEFAULT as well
    // leaves it out - the same question malloc() asks. Stacks need a contiguous 8-bit block.
    s.internal_usable = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT);
    s.internal_usable_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT);
    s.internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    float t;
    s.temp_c = (s_tsens && temperature_sensor_get_celsius(s_tsens, &t) == ESP_OK) ? (int16_t)(t + 0.5f) : INT16_MIN;
    s_cpu_hist[s_head] = s.cpu_pct;
    // The sparkline on the device shows what a malloc() would find, not the sum with the pool in it.
    s_ram_hist[s_head] = (uint16_t)(s.internal_usable / 1024);
    s_head = (s_head + 1) % AS_SYSMON_POINTS;
    if (s.count < AS_SYSMON_POINTS) s.count++;
}

const as_sysmon_t *as_sysmon_get(void) { return &s; }

void as_sysmon_history(int32_t cpu[AS_SYSMON_POINTS], int32_t ram_kb[AS_SYSMON_POINTS], int32_t none)
{
    int start = (s_head + AS_SYSMON_POINTS - s.count) % AS_SYSMON_POINTS;
    for (int i = 0; i < AS_SYSMON_POINTS; i++) {
        int missing = AS_SYSMON_POINTS - s.count;
        if (i < missing) {
            cpu[i] = ram_kb[i] = none;
        } else {
            int idx = (start + i - missing) % AS_SYSMON_POINTS;
            cpu[i] = s_cpu_hist[idx];
            ram_kb[i] = s_ram_hist[idx];
        }
    }
}
