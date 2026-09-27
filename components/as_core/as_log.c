#include "as_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LINES 128
#define JLINE_MAX 160

static char (*s_ring)[JLINE_MAX];   // in PSRAM when there is some, else internal RAM
static int s_head;                 // next slot to write
static int s_count;
static SemaphoreHandle_t s_lock;

void as_log_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_ring = heap_caps_calloc(LINES, JLINE_MAX, MALLOC_CAP_SPIRAM);
    if (!s_ring) s_ring = calloc(LINES, JLINE_MAX);
}

void as_logf(const char *fmt, ...)
{
    char line[JLINE_MAX];
    // Uptime is always there; the wall clock only once SNTP has run, and then it is the better key.
    uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    time_t now = time(NULL);
    struct tm tm;
    int n;
    if (now > 1600000000) {
        localtime_r(&now, &tm);
        n = snprintf(line, sizeof line, "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
        n = snprintf(line, sizeof line, "+%lus ", (unsigned long)up);
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof line - n, fmt, ap);
    va_end(ap);

    ESP_LOGI("journal", "%s", line + n);
    if (!s_ring || !s_lock) return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) return;
    strncpy(s_ring[s_head], line, JLINE_MAX - 1);
    s_ring[s_head][JLINE_MAX - 1] = '\0';
    s_head = (s_head + 1) % LINES;
    if (s_count < LINES) s_count++;
    xSemaphoreGive(s_lock);
}

size_t as_log_tail(char *out, size_t cap, int max_lines)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!s_ring || !s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    int take = max_lines < s_count ? max_lines : s_count;
    size_t o = 0;
    for (int i = take; i > 0; i--) {
        int idx = (s_head - i + LINES) % LINES;
        int n = snprintf(out + o, cap - o, "%s\n", s_ring[idx]);
        if (n < 0 || (size_t)n >= cap - o) break;
        o += n;
    }
    xSemaphoreGive(s_lock);
    return o;
}
