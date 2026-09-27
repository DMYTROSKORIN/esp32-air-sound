idf_component_register(SRCS "as_ota.c" INCLUDE_DIRS "include"
                       REQUIRES app_update esp_http_client esp-tls mbedtls json esp_app_format esp_timer as_core
                       PRIV_REQUIRES nvs_flash)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)
