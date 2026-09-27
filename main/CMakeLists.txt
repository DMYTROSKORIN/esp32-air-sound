idf_component_register(SRCS "main.c" "si4713.c" "tone.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_i2c esp_driver_i2s esp_driver_gpio esp_app_format)
