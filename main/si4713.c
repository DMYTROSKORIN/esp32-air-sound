#include "si4713.h"

#include <string.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "si4713";

// Commands (AN332, transmit set)
#define CMD_POWER_UP        0x01
#define CMD_GET_REV         0x10
#define CMD_POWER_DOWN      0x11
#define CMD_SET_PROPERTY    0x12
#define CMD_GET_PROPERTY    0x13
#define CMD_GET_INT_STATUS  0x14
#define CMD_TX_TUNE_FREQ    0x30
#define CMD_TX_TUNE_POWER   0x31
#define CMD_TX_TUNE_MEASURE 0x32
#define CMD_TX_TUNE_STATUS  0x33
#define CMD_TX_ASQ_STATUS   0x34
#define CMD_TX_RDS_BUFF     0x35
#define CMD_TX_RDS_PS       0x36

// Properties
#define PROP_REFCLK_FREQ            0x0201
#define PROP_REFCLK_PRESCALE        0x0202
#define PROP_TX_COMPONENT_ENABLE    0x2100
#define PROP_TX_AUDIO_DEVIATION     0x2101
#define PROP_TX_PILOT_DEVIATION     0x2102
#define PROP_TX_RDS_DEVIATION       0x2103
#define PROP_TX_LINE_INPUT_LEVEL    0x2104
#define PROP_TX_LINE_INPUT_MUTE     0x2105
#define PROP_TX_PREEMPHASIS         0x2106
#define PROP_TX_ACOMP_ENABLE        0x2200
#define PROP_TX_RDS_PI              0x2C01
#define PROP_TX_RDS_PS_MIX          0x2C02
#define PROP_TX_RDS_PS_MISC         0x2C03
#define PROP_TX_RDS_PS_REPEAT_COUNT 0x2C04
#define PROP_TX_RDS_PS_MESSAGE_COUNT 0x2C05
#define PROP_TX_RDS_PS_AF           0x2C06
#define PROP_TX_RDS_FIFO_SIZE       0x2C07

#define STATUS_CTS   0x80
#define STATUS_ERR   0x40
#define STATUS_STCINT 0x01

#define I2C_TIMEOUT_MS 100

static esp_err_t read_status(si4713_t *tx, uint8_t *status)
{
    return i2c_master_receive(tx->dev, status, 1, I2C_TIMEOUT_MS);
}

// Every command ends with the chip raising CTS; nothing else may be sent before that.
static esp_err_t wait_cts(si4713_t *tx, int timeout_ms)
{
    uint8_t st = 0;
    for (int i = 0; i < timeout_ms; i++) {
        esp_err_t err = read_status(tx, &st);
        if (err != ESP_OK) return err;
        if (st & STATUS_CTS) return (st & STATUS_ERR) ? ESP_FAIL : ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t command(si4713_t *tx, const uint8_t *cmd, size_t len, uint8_t *resp, size_t resp_len)
{
    esp_err_t err = i2c_master_transmit(tx->dev, cmd, len, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    err = wait_cts(tx, 500);
    if (err != ESP_OK) return err;
    if (resp && resp_len) return i2c_master_receive(tx->dev, resp, resp_len, I2C_TIMEOUT_MS);
    return ESP_OK;
}

// TX_TUNE_* commands raise CTS at once and STCINT when the tune has settled.
static esp_err_t wait_stc(si4713_t *tx, int timeout_ms)
{
    for (int i = 0; i < timeout_ms / 5; i++) {
        uint8_t cmd = CMD_GET_INT_STATUS, st = 0;
        esp_err_t err = command(tx, &cmd, 1, &st, 1);
        if (err != ESP_OK) return err;
        if (st & STATUS_STCINT) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t si4713_set_property(si4713_t *tx, uint16_t prop, uint16_t value)
{
    uint8_t cmd[6] = { CMD_SET_PROPERTY, 0x00, prop >> 8, prop & 0xFF, value >> 8, value & 0xFF };
    esp_err_t err = command(tx, cmd, sizeof cmd, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    return err;
}

esp_err_t si4713_get_property(si4713_t *tx, uint16_t prop, uint16_t *value)
{
    uint8_t cmd[4] = { CMD_GET_PROPERTY, 0x00, prop >> 8, prop & 0xFF };
    uint8_t resp[4];
    esp_err_t err = command(tx, cmd, sizeof cmd, resp, sizeof resp);
    if (err == ESP_OK) *value = ((uint16_t)resp[2] << 8) | resp[3];
    return err;
}

esp_err_t si4713_init(si4713_t *tx, i2c_master_bus_handle_t bus, uint8_t addr, int rst_gpio,
                      si4713_rev_t *rev)
{
    memset(tx, 0, sizeof *tx);
    tx->addr = addr;
    tx->rst_gpio = rst_gpio;

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << rst_gpio,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "rst gpio");
    gpio_set_level(rst_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rst_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t err = i2c_master_probe(bus, addr, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return ESP_ERR_NOT_FOUND;

    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev, &tx->dev), TAG, "add device");

    // ARG1 0x12: crystal oscillator on, function 2 = transmit. ARG2 0x50: analog line input.
    uint8_t pu[3] = { CMD_POWER_UP, 0x12, 0x50 };
    err = command(tx, pu, sizeof pu, NULL, 0);
    if (err != ESP_OK) { ESP_LOGE(TAG, "power up: %s", esp_err_to_name(err)); return err; }
    vTaskDelay(pdMS_TO_TICKS(110));   // crystal start-up

    uint8_t cmd = CMD_GET_REV, r[9];
    err = command(tx, &cmd, 1, r, sizeof r);
    if (err != ESP_OK) return err;
    if (rev) {
        rev->part_number = r[1];
        rev->fw_major = r[2]; rev->fw_minor = r[3];
        rev->patch_hi = r[4]; rev->patch_lo = r[5];
        rev->cmp_major = r[6]; rev->cmp_minor = r[7];
        rev->chip_rev = r[8];
    }
    return ESP_OK;
}

esp_err_t si4713_configure_audio(si4713_t *tx, bool preemph_50us)
{
    // 32.768 kHz crystal on the module, no prescaler.
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_REFCLK_FREQ, 32768), TAG, "refclk");
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_REFCLK_PRESCALE, 1), TAG, "prescale");
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_PREEMPHASIS, preemph_50us ? 1 : 0), TAG, "preemph");
    // Line input: attenuation setting 3 = 636 mVpk full scale, 60 kOhm. The PCM5102 is
    // still hotter than that at full scale; the DAC's digital level takes care of the rest.
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_LINE_INPUT_LEVEL, (3 << 12) | 636), TAG, "line level");
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_LINE_INPUT_MUTE, 0), TAG, "unmute");
    // Deviations: 68.25 kHz audio, 6.75 kHz pilot, 2 kHz RDS (the chip's defaults, set
    // explicitly so nobody has to look them up).
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_AUDIO_DEVIATION, 6825), TAG, "audio dev");
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_PILOT_DEVIATION, 675), TAG, "pilot dev");
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_RDS_DEVIATION, 200), TAG, "rds dev");
    // Limiter on, compressor off.
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_ACOMP_ENABLE, 0x0002), TAG, "acomp");
    // Pilot + left-minus-right (stereo) + RDS.
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_COMPONENT_ENABLE, 0x0007), TAG, "components");
    return ESP_OK;
}

esp_err_t si4713_tune_power(si4713_t *tx, uint8_t power_dbuv, uint8_t antcap)
{
    uint8_t cmd[5] = { CMD_TX_TUNE_POWER, 0x00, 0x00, power_dbuv, antcap };
    ESP_RETURN_ON_ERROR(command(tx, cmd, sizeof cmd, NULL, 0), TAG, "tune power");
    return wait_stc(tx, 1000);
}

esp_err_t si4713_tune_freq(si4713_t *tx, uint16_t freq_10khz)
{
    uint8_t cmd[4] = { CMD_TX_TUNE_FREQ, 0x00, freq_10khz >> 8, freq_10khz & 0xFF };
    ESP_RETURN_ON_ERROR(command(tx, cmd, sizeof cmd, NULL, 0), TAG, "tune freq");
    return wait_stc(tx, 1000);
}

esp_err_t si4713_tune_status(si4713_t *tx, si4713_tune_status_t *st)
{
    uint8_t cmd[2] = { CMD_TX_TUNE_STATUS, 0x01 };   // INTACK clears STCINT
    uint8_t r[8];
    ESP_RETURN_ON_ERROR(command(tx, cmd, sizeof cmd, r, sizeof r), TAG, "tune status");
    st->freq_10khz = ((uint16_t)r[2] << 8) | r[3];
    st->power_dbuv = r[5];
    st->antcap = r[6];
    st->noise_level = r[7];
    return ESP_OK;
}

esp_err_t si4713_asq_status(si4713_t *tx, si4713_asq_t *asq)
{
    uint8_t cmd[2] = { CMD_TX_ASQ_STATUS, 0x01 };
    uint8_t r[5];
    ESP_RETURN_ON_ERROR(command(tx, cmd, sizeof cmd, r, sizeof r), TAG, "asq");
    asq->overmod = r[1] & 0x04;
    asq->level_high = r[1] & 0x02;
    asq->level_low = r[1] & 0x01;
    asq->in_level_db = (int8_t)r[4];
    return ESP_OK;
}

esp_err_t si4713_rds_begin(si4713_t *tx, uint16_t pi)
{
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_RDS_PI, pi), TAG, "pi");
    // Mix PS with the buffer 50/50, music flag + stereo in MISC, PS repeated 3 times, one
    // PS message, no alternative frequencies, whole buffer circular.
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_RDS_PS_MIX, 0x0003), TAG, "ps mix");
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_RDS_PS_MISC, 0x1808), TAG, "ps misc");
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_RDS_PS_REPEAT_COUNT, 3), TAG, "ps repeat");
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_RDS_PS_MESSAGE_COUNT, 1), TAG, "ps count");
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_RDS_PS_AF, 0xE0E0), TAG, "ps af");
    ESP_RETURN_ON_ERROR(si4713_set_property(tx, PROP_TX_RDS_FIFO_SIZE, 0), TAG, "fifo");
    return ESP_OK;
}

esp_err_t si4713_rds_set_ps(si4713_t *tx, const char *ps8)
{
    char ps[8];
    memset(ps, ' ', sizeof ps);
    size_t n = strlen(ps8);
    memcpy(ps, ps8, n > 8 ? 8 : n);
    for (int seg = 0; seg < 2; seg++) {
        uint8_t cmd[6] = { CMD_TX_RDS_PS, (uint8_t)seg,
                           ps[seg * 4], ps[seg * 4 + 1], ps[seg * 4 + 2], ps[seg * 4 + 3] };
        ESP_RETURN_ON_ERROR(command(tx, cmd, sizeof cmd, NULL, 0), TAG, "rds ps");
    }
    return ESP_OK;
}

esp_err_t si4713_rds_set_radiotext(si4713_t *tx, const char *text)
{
    char rt[64];
    memset(rt, ' ', sizeof rt);
    size_t n = strlen(text);
    if (n > 64) n = 64;
    memcpy(rt, text, n);
    size_t segments = (n + 3) / 4;
    if (segments == 0) segments = 1;
    for (size_t seg = 0; seg < segments; seg++) {
        // Group 2A: block B = 0x2000 | segment address; first write also clears the buffer
        // (ctrl 0x06 = LDBUFF | MTBUFF), later ones only load (0x04).
        uint8_t cmd[8] = { CMD_TX_RDS_BUFF, seg == 0 ? 0x06 : 0x04,
                           0x20, (uint8_t)seg,
                           rt[seg * 4], rt[seg * 4 + 1], rt[seg * 4 + 2], rt[seg * 4 + 3] };
        uint8_t resp[6];
        ESP_RETURN_ON_ERROR(command(tx, cmd, sizeof cmd, resp, sizeof resp), TAG, "rds buff");
    }
    // Re-assert pilot + stereo + RDS now that the buffers hold something; the reference
    // sequence enables the RDS component after the buffers are set up, not before.
    return si4713_set_property(tx, PROP_TX_COMPONENT_ENABLE, 0x0007);
}

esp_err_t si4713_rds_status(si4713_t *tx, si4713_rds_status_t *st)
{
    uint8_t cmd[8] = { CMD_TX_RDS_BUFF, 0x00, 0, 0, 0, 0, 0, 0 };   // no load, no clear
    uint8_t r[6];
    ESP_RETURN_ON_ERROR(command(tx, cmd, sizeof cmd, r, sizeof r), TAG, "rds status");
    st->ps_xmit   = r[1] & 0x10;
    st->cbuf_xmit = r[1] & 0x08;
    st->fifo_xmit = r[1] & 0x04;
    st->cbuf_used = r[3];
    return si4713_get_property(tx, PROP_TX_COMPONENT_ENABLE, &st->component_enable);
}
