// esp32-air-sound: an AirPlay speaker whose loudspeaker is every FM radio in the flat.
//
// Boot: configuration from NVS, LED and button, transmitter on air with silence, then Wi-Fi.
// Without credentials (or after a 5 s BOOT hold) the setup portal opens instead. On the network:
// mDNS, the release checker, and the AirPlay receiver (step 2).

#include <stdio.h>
#include <string.h>

#include "as_config.h"
#include "as_log.h"
#include "as_net.h"
#include "as_ota.h"
#include "as_provision.h"
#include "airplay.h"
#include "audio_out.h"
#include "board.h"
#include "button.h"
#include "driver/i2c_master.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fm.h"
#include "led_status.h"
#include "mdns.h"
#include "nvs.h"

static const char *TAG = "app";
static bool s_setup_mode;
static bool s_streaming;
static bool s_net_up;

static const char *reset_reason_name(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt-watchdog";
    case ESP_RST_TASK_WDT: return "task-watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    default: return "other";
    }
}

// Boot counter in its own NVS namespace, so a factory reset (which erases "as") keeps the history.
static uint32_t count_boot(void)
{
    nvs_handle_t h;
    uint32_t boots = 0;
    if (nvs_open("sys", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, "boots", &boots);
        boots++;
        nvs_set_u32(h, "boots", boots);
        nvs_commit(h);
        nvs_close(h);
    }
    return boots;
}

static void update_led(void)
{
    if (s_setup_mode) led_status_set(LED_SETUP);
    else if (!fm_ok()) led_status_set(LED_FAULT);
    else if (!s_net_up) led_status_set(LED_CONNECTING);
    else led_status_set(s_streaming ? LED_STREAMING : LED_ONLINE);
}

static void start_mdns(void)
{
    static bool started;
    if (started) return;
    started = true;
    const as_config_t *c = as_config_get();
    if (mdns_init() != ESP_OK) return;
    mdns_hostname_set(as_net_hostname());
    mdns_instance_name_set(c->name);
    as_logf("mdns: %s.local", as_net_hostname());
}

static void net_cb(as_net_state_t st, void *user)
{
    switch (st) {
    case AS_NET_UP:
        s_net_up = true;
        // Modem sleep costs up to 300 ms of latency per packet; an audio receiver on USB power
        // keeps the radio awake, as bastion does.
        esp_wifi_set_ps(WIFI_PS_NONE);
        start_mdns();
        airplay_start();
        as_ota_note_service_up();
        break;
    case AS_NET_PROVISIONING:
        s_setup_mode = true;
        break;
    default:
        s_net_up = false;
        break;
    }
    update_led();
}

static void button_cb(button_event_t ev)
{
    switch (ev) {
    case BUTTON_SHORT:
        as_logf("button: short press, play/pause");
        airplay_toggle();
        break;
    case BUTTON_SETUP:
        as_logf("button: 5 s hold, opening the setup portal");
        s_setup_mode = true;
        update_led();
        as_net_start_provisioning();
        break;
    case BUTTON_RESET:
        as_logf("button: 10 s hold, factory reset");
        led_status_reset_flash();
        as_config_erase();
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
        break;
    }
}

static void airplay_state_cb(bool streaming)
{
    s_streaming = streaming;
    as_logf("airplay: %s", streaming ? "streaming" : "idle");
    update_led();
}

static bool ota_may_install(void)
{
    return !s_streaming;   // never mid-song
}

void app_main(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    as_config_init();
    as_log_init();
    uint32_t boots = count_boot();
    as_logf("esp32-air-sound %s, boot #%lu after %s reset", desc->version, (unsigned long)boots, reset_reason_name());
    ESP_LOGI(TAG, "pins: I2C SDA=%d SCL=%d RST=%d | I2S BCK=%d LRCK=%d DOUT=%d | LED=%d BOOT=%d", PIN_I2C_SDA,
             PIN_I2C_SCL, PIN_SI4713_RST, PIN_I2S_BCK, PIN_I2S_LRCK, PIN_I2S_DOUT, PIN_LED, PIN_BUTTON);

    led_status_init(PIN_LED);
    button_init(PIN_BUTTON, button_cb);

    // Transmitter first: the radios should hear silence, not noise, while the network comes up.
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));
    fm_init(bus, PIN_SI4713_RST);
    ESP_ERROR_CHECK(audio_out_init(PIN_I2S_BCK, PIN_I2S_LRCK, PIN_I2S_DOUT));
    update_led();

    airplay_init(airplay_state_cb);

    // Network: station with the stored credentials, or the setup portal.
    as_net_init(net_cb, NULL);
    as_net_start();
    if (!as_config_is_provisioned()) {
        s_setup_mode = true;
        update_led();
    }

    // Updates: signed releases from GitHub, checked two minutes after boot and daily; installed
    // by themselves only when the owner ticked that and nothing is streaming.
    as_ota_configure_release_check("DMYTROSKORIN/esp32-air-sound", "v", "esp32-air-sound-signed.bin");
    as_ota_self_test_start();
    as_ota_checker_start(ota_may_install);

    char line[160];
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(s_streaming ? 5000 : 30000));
        fm_status_line(line, sizeof line);
        ESP_LOGI(TAG, "%s | net %s rssi %d | audio %s, queued %u ms, min %u ms, underruns %lu | heap %lu", line,
                 s_net_up ? "up" : "down", as_net_rssi(), audio_out_active() ? "streaming" : "silent",
                 (unsigned)(audio_out_queued() * 1000 / 44100), (unsigned)(audio_out_min_queued() * 1000 / 44100),
                 (unsigned long)audio_out_underruns(), (unsigned long)esp_get_free_heap_size());
        if (s_streaming) ESP_LOGI(TAG, "decoded stream: %lu jumps", (unsigned long)airplay_jumps());
        update_led();
    }
}
