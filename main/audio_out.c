#include "audio_out.h"

#include <math.h>
#include <string.h>
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "audio";

#define SAMPLE_RATE 44100
#define RING_FRAMES (SAMPLE_RATE * 2)     // 2 s
#define CHUNK_FRAMES 441                  // 10 ms per I2S write

static i2s_chan_handle_t s_tx;
static int16_t *s_ring;                   // interleaved L R, RING_FRAMES frames, in PSRAM
static volatile size_t s_rd, s_wr;        // frame indices
static SemaphoreHandle_t s_lock;
static volatile bool s_tone;
static volatile bool s_active;
static volatile int32_t s_gain_q16;       // 65536 = 0 dB (before the trim)
static float s_gain_db;
static int32_t s_trim_q16;
static int32_t s_out[CHUNK_FRAMES * 2];
static int16_t s_tone_tab[CHUNK_FRAMES];

static int32_t db_to_q16(float db)
{
    if (db <= -144.0f) return 0;
    float g = powf(10.0f, db / 20.0f);
    if (g > 1.0f) g = 1.0f;
    return (int32_t)lrintf(g * 65536.0f);
}

static size_t queued(void)
{
    size_t rd = s_rd, wr = s_wr;
    return wr >= rd ? wr - rd : RING_FRAMES - rd + wr;
}

size_t audio_out_queued(void) { return queued(); }

size_t audio_out_write(const int16_t *frames, size_t nframes)
{
    if (!s_ring) return 0;
    size_t space = RING_FRAMES - 1 - queued();
    if (nframes > space) nframes = space;
    size_t wr = s_wr;
    size_t first = RING_FRAMES - wr;
    if (first > nframes) first = nframes;
    memcpy(&s_ring[wr * 2], frames, first * 4);
    if (nframes > first) memcpy(&s_ring[0], frames + first * 2, (nframes - first) * 4);
    s_wr = (wr + nframes) % RING_FRAMES;
    return nframes;
}

void audio_out_flush(void)
{
    s_rd = s_wr;
}

void audio_out_set_gain_db(float db)
{
    if (db > 0) db = 0;
    s_gain_db = db;
    s_gain_q16 = db_to_q16(db);
}

float audio_out_gain_db(void) { return s_gain_db; }
void audio_out_set_tone(bool on) { s_tone = on; }
bool audio_out_active(void) { return s_active; }

static void out_task(void *arg)
{
    uint32_t n = 0;
    for (;;) {
        // Total gain: user gain x trim, both Q16, product back to Q16 (max 65536 x 65536 >> 16).
        int64_t gain = ((int64_t)s_gain_q16 * s_trim_q16) >> 16;
        size_t have = queued();
        bool from_ring = have >= CHUNK_FRAMES;
        if (from_ring) {
            size_t rd = s_rd;
            for (int i = 0; i < CHUNK_FRAMES; i++) {
                size_t idx = (rd + i) % RING_FRAMES;
                s_out[2 * i] = (int32_t)(((int64_t)s_ring[idx * 2] * gain) >> 0);
                s_out[2 * i + 1] = (int32_t)(((int64_t)s_ring[idx * 2 + 1] * gain) >> 0);
            }
            s_rd = (rd + CHUNK_FRAMES) % RING_FRAMES;
        } else if (s_tone && ((n / 50) & 1) == 0) {
            for (int i = 0; i < CHUNK_FRAMES; i++) {
                int32_t v = (int32_t)(((int64_t)s_tone_tab[i] * s_trim_q16));
                s_out[2 * i] = v;
                s_out[2 * i + 1] = v;
            }
        } else {
            memset(s_out, 0, sizeof s_out);
        }
        s_active = from_ring;
        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx, s_out, sizeof s_out, &written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s write: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        n++;
    }
}

esp_err_t audio_out_init(int bck_gpio, int lrck_gpio, int dout_gpio)
{
    s_ring = heap_caps_calloc(RING_FRAMES * 2, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_ring) s_ring = calloc(RING_FRAMES * 2, sizeof(int16_t));
    if (!s_ring) return ESP_ERR_NO_MEM;
    s_lock = xSemaphoreCreateMutex();
    s_trim_q16 = db_to_q16(-(float)CONFIG_AIRSOUND_LINE_TRIM_DB);
    audio_out_set_gain_db(0);
#ifdef CONFIG_AIRSOUND_TONE_ENABLE
    s_tone = true;
#else
    s_tone = false;
#endif
    float amp = 32767.0f * powf(10.0f, -(float)CONFIG_AIRSOUND_TONE_DB / 20.0f);
    for (int i = 0; i < CHUNK_FRAMES; i++)
        s_tone_tab[i] = (int16_t)lrintf(amp * sinf(2.0f * (float)M_PI * 1000.0f * i / SAMPLE_RATE));

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    chan.dma_desc_num = 8;
    chan.dma_frame_num = CHUNK_FRAMES;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &s_tx, NULL), TAG, "channel");
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    // PCM5102 SCK tied to GND, PLL from BCK
            .bclk = bck_gpio,
            .ws = lrck_gpio,
            .dout = dout_gpio,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {0},
        },
    };
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "std mode");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "enable");
    if (xTaskCreatePinnedToCore(out_task, "audio_out", 4096, NULL, 20, NULL, 1) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "44.1 kHz / 32-bit slots on BCK=%d LRCK=%d DOUT=%d, trim -%d dB, ring %d ms%s", bck_gpio, lrck_gpio,
             dout_gpio, CONFIG_AIRSOUND_LINE_TRIM_DB, RING_FRAMES * 1000 / SAMPLE_RATE, s_tone ? ", tone on" : "");
    return ESP_OK;
}
