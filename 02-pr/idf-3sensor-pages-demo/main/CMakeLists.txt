idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_event esp_netif esp_wifi json led_strip mqtt nvs_flash)
