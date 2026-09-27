idf_component_register(SRCS "main.c" "si4713.c" "fm.c" "audio_out.c" "led_status.c" "button.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_i2c esp_driver_i2s esp_driver_gpio esp_app_format esp_timer
                                     as_core as_net as_ota as_sysmon app_update nvs_flash esp_wifi)
