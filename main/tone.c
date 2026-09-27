#include "tone.h"

#include <math.h>
#include <stdlib.h>
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "tone";

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUF 441     // 10 ms; a whole number of cycles for any tone that divides 100 Hz

static i2s_chan_handle_t s_tx;
static int16_t s_buf[FRAMES_PER_BUF * 2];
static int16_t s_silence[FRAMES_PER_BUF * 2];
static bool s_enable;

// 500 ms of tone, 500 ms of silence: a pattern no station and no noise will imitate.
#define BEEP_BUFS 50

static void tone_task(void *arg)
{
    int n = 0;
    for (;;) {
        const int16_t *src = (!s_enable || ((n / BEEP_BUFS) & 1)) ? s_silence : s_buf;
        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx, src, sizeof s_buf, &written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "write: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        n++;
    }
}

esp_err_t tone_start(int bck_gpio, int lrck_gpio, int dout_gpio, int tone_hz, int level_db, bool enable)
{
    s_enable = enable;
    // Phase-accurate table: the buffer is exactly 10 ms, so a tone whose frequency is a
    // multiple of 100 Hz repeats seamlessly. For others there is a small click every 10 ms,
    // which is fine for a bench tone but is why the default is 1 kHz.
    float amp = 32767.0f * powf(10.0f, -(float)level_db / 20.0f);
    for (int i = 0; i < FRAMES_PER_BUF; i++) {
        int16_t v = (int16_t)lrintf(amp * sinf(2.0f * (float)M_PI * tone_hz * i / SAMPLE_RATE));
        s_buf[2 * i] = v;       // left
        s_buf[2 * i + 1] = v;   // right
    }

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &s_tx, NULL), TAG, "channel");

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,   // PCM5102 SCK is tied to GND, PLL runs from BCK
            .bclk = bck_gpio,
            .ws = lrck_gpio,
            .dout = dout_gpio,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { 0 },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "std mode");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "enable");

    if (xTaskCreate(tone_task, "tone", 3072, NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    if (enable)
        ESP_LOGI(TAG, "%d Hz at -%d dBFS, 0.5 s on / 0.5 s off, 44.1 kHz/16-bit stereo on BCK=%d LRCK=%d DOUT=%d",
                 tone_hz, level_db, bck_gpio, lrck_gpio, dout_gpio);
    else
        ESP_LOGI(TAG, "tone off: silence on BCK=%d LRCK=%d DOUT=%d", bck_gpio, lrck_gpio, dout_gpio);
    return ESP_OK;
}
