#include "led_status.h"

#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

static led_strip_handle_t s_strip;
static volatile led_state_t s_state = LED_CONNECTING;
static volatile uint32_t s_hold_ms;
static uint32_t s_state_since;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }

static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    static uint8_t pr = 255, pg = 255, pb = 255;
    if (r == pr && g == pg && b == pb) return;
    pr = r; pg = g; pb = b;
    if (!s_strip) return;
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

// Brightness of a short flash `t` ms into a window of `dur` ms, eased in and out over `edge` ms.
static uint8_t eased(uint32_t t, uint32_t dur, uint8_t peak, uint32_t edge)
{
    if (t >= dur) return 0;
    if (t < edge) return (uint8_t)((uint32_t)peak * t / edge);
    uint32_t rem = dur - t;
    if (rem < edge) return (uint8_t)((uint32_t)peak * rem / edge);
    return peak;
}

// A burst of `count` identical flashes starting at `start` ms into the cycle.
static uint8_t burst(uint32_t phase, uint32_t start, uint8_t count, uint32_t flash, uint32_t gap, uint8_t peak, uint32_t edge)
{
    if (!count || phase < start) return 0;
    uint32_t t = phase - start, step = flash + gap, idx = t / step;
    if (idx >= count) return 0;
    return eased(t - idx * step, flash, peak, edge);
}

static void render(void)
{
    uint32_t now = now_ms();
    uint32_t held = s_hold_ms;
    if (held >= 3000) {
        // Amber blink while BOOT is held; faster past 5 s to say a longer hold now means reset.
        uint32_t period = held >= 5000 ? 250 : 500;
        bool on = (now % period) < period / 2;
        set_rgb(on ? 45 : 0, on ? 22 : 0, 0);
        return;
    }
    static led_state_t last = -1;
    if (s_state != last) {
        last = s_state;
        s_state_since = now;
    }
    uint32_t el = now - s_state_since;
    switch (s_state) {
    case LED_SETUP:
        set_rgb(40, 20, 0);
        break;
    case LED_CONNECTING: {
        const uint32_t breath = 1800;
        float ph = (float)(el % breath) / (float)breath;
        float lvl = (sinf(ph * 2.0f * (float)M_PI - (float)M_PI / 2.0f) + 1.0f) * 0.5f;
        set_rgb(0, 0, (uint8_t)(lvl * 38.0f));
        break;
    }
    case LED_ONLINE:
    case LED_STREAMING: {
        const uint32_t cycle = 3600;
        uint32_t ph = el % cycle;
        uint8_t g = burst(ph, 0, 2, 100, 100, 40, 20);
        uint8_t v = s_state == LED_STREAMING ? 1 : 0;
        uint8_t vr = burst(ph, 2600, v, 150, 150, 50, 20);
        uint8_t vb = burst(ph, 2600, v, 150, 150, 70, 20);
        set_rgb(vr, g, vb);
        break;
    }
    case LED_FAULT:
        set_rgb((el % 200) < 100 ? 50 : 0, 0, 0);
        break;
    }
}

static void led_task(void *arg)
{
    for (;;) {
        render();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void led_status_init(int gpio)
{
    led_strip_config_t sc = {
        .strip_gpio_num = gpio,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rc = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    if (led_strip_new_rmt_device(&sc, &rc, &s_strip) != ESP_OK) {
        ESP_LOGW("led", "no LED strip on GPIO %d", gpio);
        s_strip = NULL;
    }
    s_state_since = now_ms();
    xTaskCreatePinnedToCore(led_task, "led", 2560, NULL, 2, NULL, 0);
}

void led_status_set(led_state_t st) { s_state = st; }
void led_status_hold(uint32_t held_ms) { s_hold_ms = held_ms; }

void led_status_reset_flash(void)
{
    uint32_t t0 = now_ms();
    s_hold_ms = 0;
    while (now_ms() - t0 < 1500) {
        set_rgb((now_ms() % 200) < 100 ? 60 : 0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    set_rgb(0, 0, 0);
}
