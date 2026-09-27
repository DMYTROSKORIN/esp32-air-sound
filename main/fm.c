#include "fm.h"

#include <stdio.h>
#include <string.h>
#include "as_config.h"
#include "as_log.h"
#include "board.h"
#include "esp_log.h"
#include "si4713.h"

static const char *TAG = "fm";
static si4713_t s_tx;
static bool s_ok;
static char s_rt[65];

static void load_rds(void)
{
    const as_config_t *c = as_config_get();
    si4713_rds_begin(&s_tx, c->rds_pi);
    si4713_rds_set_ps(&s_tx, c->rds_ps);
    si4713_rds_set_radiotext(&s_tx, s_rt[0] ? s_rt : c->rds_rt);
}

bool fm_init(i2c_master_bus_handle_t bus, int rst_gpio)
{
    si4713_rev_t rev;
    uint8_t addr = SI4713_ADDR_CS_HIGH;
    esp_err_t err = si4713_init(&s_tx, bus, addr, rst_gpio, &rev);
    if (err == ESP_ERR_NOT_FOUND) {
        addr = SI4713_ADDR_CS_LOW;
        err = si4713_init(&s_tx, bus, addr, rst_gpio, &rev);
    }
    if (err != ESP_OK) {
        as_logf("fm: no Si4713 on I2C (%s)", esp_err_to_name(err));
        s_ok = false;
        return false;
    }
    as_logf("fm: si4713 at 0x%02X, part %u, firmware %c.%c, rev %c", addr, rev.part_number, rev.fw_major, rev.fw_minor,
            rev.chip_rev);
    s_ok = true;
    return fm_apply_config();
}

bool fm_ok(void) { return s_ok; }

bool fm_apply_config(void)
{
    if (!s_ok) return false;
    const as_config_t *c = as_config_get();
    esp_err_t err = si4713_configure_audio(&s_tx, c->preemph_50us);
    if (err == ESP_OK) err = si4713_tune_power(&s_tx, c->tx_power, 0);
    if (err == ESP_OK) err = si4713_tune_freq(&s_tx, c->freq_10khz);
    if (err != ESP_OK) {
        as_logf("fm: tune failed: %s", esp_err_to_name(err));
        return false;
    }
    load_rds();
    si4713_tune_status_t st;
    if (si4713_tune_status(&s_tx, &st) == ESP_OK) {
        as_logf("fm: on air %u.%02u MHz, %u dBuV, antenna cap %u, RDS \"%s\"", st.freq_10khz / 100, st.freq_10khz % 100,
                st.power_dbuv, st.antcap, c->rds_ps);
        if (st.antcap == 0 || st.antcap == 191) as_logf("fm: antenna cap at the end of its range, check the antenna wire");
    }
    return true;
}

void fm_set_now_playing(const char *artist, const char *title)
{
    if (!s_ok) return;
    if (artist && artist[0] && title && title[0]) snprintf(s_rt, sizeof s_rt, "%s - %s", artist, title);
    else if (title && title[0]) snprintf(s_rt, sizeof s_rt, "%s", title);
    else if (artist && artist[0]) snprintf(s_rt, sizeof s_rt, "%s", artist);
    else s_rt[0] = '\0';
    // RDS radiotext is Latin-1 at best; anything beyond ASCII becomes '?' rather than garbage.
    for (char *p = s_rt; *p; p++)
        if ((unsigned char)*p > 0x7E || (unsigned char)*p < 0x20) *p = '?';
    si4713_rds_set_radiotext(&s_tx, s_rt[0] ? s_rt : as_config_get()->rds_rt);
    ESP_LOGI(TAG, "rds rt: %s", s_rt[0] ? s_rt : as_config_get()->rds_rt);
}

void fm_clear_now_playing(void)
{
    s_rt[0] = '\0';
    if (s_ok) si4713_rds_set_radiotext(&s_tx, as_config_get()->rds_rt);
}

void fm_status_line(char *out, size_t cap)
{
    if (!s_ok) {
        snprintf(out, cap, "transmitter: not found");
        return;
    }
    si4713_tune_status_t st;
    si4713_asq_t asq;
    si4713_rds_status_t rs;
    if (si4713_tune_status(&s_tx, &st) != ESP_OK || si4713_asq_status(&s_tx, &asq) != ESP_OK ||
        si4713_rds_status(&s_tx, &rs) != ESP_OK) {
        snprintf(out, cap, "transmitter: not answering");
        s_ok = false;
        return;
    }
    snprintf(out, cap, "tx %u.%02u MHz %u dBuV cap %u | in %d dBfs%s | rds ps=%d rt=%d", st.freq_10khz / 100,
             st.freq_10khz % 100, st.power_dbuv, st.antcap, asq.in_level_db, asq.overmod ? " OVERMOD" : "", rs.ps_xmit,
             rs.cbuf_xmit);
}
