file(GLOB_RECURSE app_sources "${CMAKE_CURRENT_LIST_DIR}/*.c")

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    REQUIRES driver esp_adc esp_event esp_netif esp_wifi json led_strip mqtt nvs_flash
)
