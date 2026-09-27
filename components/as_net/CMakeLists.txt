idf_component_register(SRCS "as_net.c" "as_provision.c" INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_netif esp_event esp_http_server esp_http_client esp_hw_support app_update json lwip as_core)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)
