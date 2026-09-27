idf_component_register(SRCS "as_config.c" "as_log.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash esp_app_format esp_timer)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)
