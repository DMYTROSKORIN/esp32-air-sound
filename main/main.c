// esp32-air-sound, step 1: prove the bench. Scan I2C, bring the Si4713 up, put a test
// tone through the PCM5102, go on air with RDS, and keep reporting what the transmitter
// sees on its input.

#include <stdio.h>
#include "board.h"
#include "sdkconfig.h"
#include "si4713.h"
#include "tone.h"

#include "driver/i2c_master.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app";

#include "driver/gpio.h"
#include "esp_rom_sys.h"

// Reads the two I2C lines as plain inputs, with the transmitter held in reset and then
// released, and checks that they are not shorted to each other. Prints what a meter would.
static void bus_diag(void)
{
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_I2C_SDA) | (1ULL << PIN_I2C_SCL),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&in);
    gpio_config_t out = { .pin_bit_mask = 1ULL << PIN_SI4713_RST, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&out);

    gpio_set_level(PIN_SI4713_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "diag: RST low  -> SDA=%d SCL=%d (1 = idle high, expected)",
             gpio_get_level(PIN_I2C_SDA), gpio_get_level(PIN_I2C_SCL));
    gpio_set_level(PIN_SI4713_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "diag: RST high -> SDA=%d SCL=%d", gpio_get_level(PIN_I2C_SDA), gpio_get_level(PIN_I2C_SCL));

    // Pull one line down and look at the other: a short between SDA and SCL shows here.
    gpio_set_direction(PIN_I2C_SDA, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(PIN_I2C_SDA, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    int scl_while_sda_low = gpio_get_level(PIN_I2C_SCL);
    gpio_set_level(PIN_I2C_SDA, 1);
    gpio_set_direction(PIN_I2C_SCL, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(PIN_I2C_SCL, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    int sda_while_scl_low = gpio_get_level(PIN_I2C_SDA);
    gpio_set_level(PIN_I2C_SCL, 1);
    ESP_LOGI(TAG, "diag: SDA pulled low -> SCL=%d; SCL pulled low -> SDA=%d (both 1 = no short between them)",
             scl_while_sda_low, sda_while_scl_low);
    // Can the ESP32 pull each line low at all? (0 = yes; 1 = something holds it high hard)
    gpio_set_level(PIN_I2C_SDA, 0); vTaskDelay(pdMS_TO_TICKS(2));
    int sda_low_ok = gpio_get_level(PIN_I2C_SDA) == 0;
    gpio_set_level(PIN_I2C_SDA, 1);
    gpio_set_level(PIN_I2C_SCL, 0); vTaskDelay(pdMS_TO_TICKS(2));
    int scl_low_ok = gpio_get_level(PIN_I2C_SCL) == 0;
    gpio_set_level(PIN_I2C_SCL, 1);
    ESP_LOGI(TAG, "diag: can drive low: SDA=%s SCL=%s", sda_low_ok ? "yes" : "NO", scl_low_ok ? "yes" : "NO");
    // Bus recovery: a slave stuck mid-transfer releases SDA after up to nine SCL pulses.
    gpio_set_level(PIN_I2C_SDA, 1);
    for (int i = 0; i < 9; i++) {
        gpio_set_level(PIN_I2C_SCL, 0); esp_rom_delay_us(5);
        gpio_set_level(PIN_I2C_SCL, 1); esp_rom_delay_us(5);
    }
    gpio_set_level(PIN_I2C_SDA, 0); esp_rom_delay_us(5);   // STOP: SDA rises while SCL high
    gpio_set_level(PIN_I2C_SDA, 1); esp_rom_delay_us(5);
    ESP_LOGI(TAG, "diag: after 9-clock recovery -> SDA=%d SCL=%d", gpio_get_level(PIN_I2C_SDA), gpio_get_level(PIN_I2C_SCL));

    // Live view for 20 s so a wire can be pulled while watching the log.
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "diag: t=%2ds SDA=%d SCL=%d", i + 1, gpio_get_level(PIN_I2C_SDA), gpio_get_level(PIN_I2C_SCL));
    }
    gpio_reset_pin(PIN_I2C_SDA);
    gpio_reset_pin(PIN_I2C_SCL);
}

static int i2c_scan(i2c_master_bus_handle_t bus, uint8_t *found, int max)
{
    int n = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(bus, a, 20) == ESP_OK) {
            ESP_LOGI(TAG, "i2c: device at 0x%02X", a);
            if (n < max) found[n] = a;
            n++;
        }
    }
    if (n == 0) ESP_LOGW(TAG, "i2c: nothing answered on SDA=%d SCL=%d", PIN_I2C_SDA, PIN_I2C_SCL);
    return n;
}

void app_main(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "esp32-air-sound %s, step 1 bench test", desc->version);
    ESP_LOGI(TAG, "pins: I2C SDA=%d SCL=%d RST=%d | I2S BCK=%d LRCK=%d DOUT=%d",
             PIN_I2C_SDA, PIN_I2C_SCL, PIN_SI4713_RST, PIN_I2S_BCK, PIN_I2S_LRCK, PIN_I2S_DOUT);

    bus_diag();

    // --- I2C bus and scan -----------------------------------------------------------
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,   // the module has 10k, these only help
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    uint8_t found[8];
    int nfound = i2c_scan(bus, found, 8);

    // --- Si4713 ---------------------------------------------------------------------
    si4713_t fm;
    si4713_rev_t rev;
    uint8_t addr = SI4713_ADDR_CS_HIGH;
    esp_err_t err = si4713_init(&fm, bus, addr, PIN_SI4713_RST, &rev);
    if (err == ESP_ERR_NOT_FOUND) {
        addr = SI4713_ADDR_CS_LOW;
        err = si4713_init(&fm, bus, addr, PIN_SI4713_RST, &rev);
    }
    bool fm_ok = (err == ESP_OK);
    if (fm_ok) {
        // Firmware and component revisions come back as ASCII digits.
        ESP_LOGI(TAG, "si4713 at 0x%02X: part %u, firmware %c.%c, patch %02X%02X, component %c.%c, chip rev %c",
                 addr, rev.part_number, rev.fw_major, rev.fw_minor, rev.patch_hi, rev.patch_lo,
                 rev.cmp_major, rev.cmp_minor, rev.chip_rev);
        if (rev.part_number != 13) ESP_LOGW(TAG, "part number is not 13: is this really an Si4713?");
    } else {
        ESP_LOGE(TAG, "si4713: not found (%s). %d I2C device(s) seen. Check RST wire (GPIO%d), SDA/SCL, VIN.",
                 esp_err_to_name(err), nfound, PIN_SI4713_RST);
    }

    if (fm_ok) {
        ESP_ERROR_CHECK(si4713_configure_audio(&fm, CONFIG_AIRSOUND_PREEMPHASIS_50US));
        ESP_ERROR_CHECK(si4713_tune_power(&fm, CONFIG_AIRSOUND_TX_POWER_DBUV, 0));
        ESP_ERROR_CHECK(si4713_tune_freq(&fm, CONFIG_AIRSOUND_FREQ_10KHZ));
        ESP_ERROR_CHECK(si4713_rds_begin(&fm, CONFIG_AIRSOUND_RDS_PI));
        ESP_ERROR_CHECK(si4713_rds_set_ps(&fm, CONFIG_AIRSOUND_RDS_PS));
        ESP_ERROR_CHECK(si4713_rds_set_radiotext(&fm, CONFIG_AIRSOUND_RDS_RT));

        si4713_rds_status_t rs;
        if (si4713_rds_status(&fm, &rs) == ESP_OK)
            ESP_LOGI(TAG, "rds: components 0x%04X (pilot%s stereo%s rds%s), PS xmit %d, RT groups %u xmit %d",
                     rs.component_enable, (rs.component_enable & 1) ? "+" : "-",
                     (rs.component_enable & 2) ? "+" : "-", (rs.component_enable & 4) ? "+" : "-",
                     rs.ps_xmit, rs.cbuf_used, rs.cbuf_xmit);

        si4713_tune_status_t st;
        if (si4713_tune_status(&fm, &st) == ESP_OK) {
            ESP_LOGI(TAG, "on air: %u.%02u MHz, %u dBuV, antenna cap %u (%.2f pF), noise %u dBuV, RDS PS \"%s\"",
                     st.freq_10khz / 100, st.freq_10khz % 100, st.power_dbuv, st.antcap,
                     st.antcap * 0.25f, st.noise_level, CONFIG_AIRSOUND_RDS_PS);
            if (st.antcap == 0 || st.antcap == 191)
                ESP_LOGW(TAG, "antenna cap at the end of its range: antenna wire missing or wrong length?");
        }
    }

    // --- Test tone to the DAC (runs even without the transmitter, so the DAC can be
    //     checked on headphones) --------------------------------------------------------
    ESP_ERROR_CHECK(tone_start(PIN_I2S_BCK, PIN_I2S_LRCK, PIN_I2S_DOUT,
                               CONFIG_AIRSOUND_TONE_HZ, CONFIG_AIRSOUND_TONE_DB));

    // --- Report -----------------------------------------------------------------------
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (!fm_ok) continue;
        si4713_asq_t asq;
        si4713_tune_status_t st;
        si4713_rds_status_t rs;
        if (si4713_asq_status(&fm, &asq) == ESP_OK && si4713_tune_status(&fm, &st) == ESP_OK &&
            si4713_rds_status(&fm, &rs) == ESP_OK) {
            ESP_LOGI(TAG, "tx %u.%02u MHz %u dBuV cap %u | audio in %d dBfs%s | rds ps=%d rt=%d",
                     st.freq_10khz / 100, st.freq_10khz % 100, st.power_dbuv, st.antcap,
                     asq.in_level_db, asq.overmod ? " OVERMOD" : "", rs.ps_xmit, rs.cbuf_xmit);
        }
    }
}
