#include "button.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_status.h"

#define SETUP_MS 5000
#define RESET_MS 10000
#define DEBOUNCE_MS 30

static int s_gpio;
static button_cb_t s_cb;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }

static void button_task(void *arg)
{
    // Wait for a release first: a low level at start is not a press.
    while (gpio_get_level(s_gpio) == 0) vTaskDelay(pdMS_TO_TICKS(50));
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (gpio_get_level(s_gpio) != 0) continue;
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        if (gpio_get_level(s_gpio) != 0) continue;
        uint32_t t0 = now_ms();
        bool reset_fired = false;
        while (gpio_get_level(s_gpio) == 0) {
            uint32_t held = now_ms() - t0;
            led_status_hold(held);
            if (held >= RESET_MS) {
                reset_fired = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        uint32_t held = now_ms() - t0;
        led_status_hold(0);
        if (reset_fired) s_cb(BUTTON_RESET);
        else if (held >= SETUP_MS) s_cb(BUTTON_SETUP);
        else if (held >= DEBOUNCE_MS) s_cb(BUTTON_SHORT);
        while (gpio_get_level(s_gpio) == 0) vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void button_init(int gpio, button_cb_t cb)
{
    s_gpio = gpio;
    s_cb = cb;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    xTaskCreatePinnedToCore(button_task, "button", 3072, NULL, 3, NULL, 0);
}
