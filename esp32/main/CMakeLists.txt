idf_component_register(
    SRCS
        "app_main.c"
        "config_manager.c"
        "wifi_manager.c"
        "button_manager.c"
        "event_manager.c"
        "output_manager.c"
        "web_server.c"
        "rf_receiver.c"
    INCLUDE_DIRS "."
    EMBED_FILES "index.html"
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        mqtt
        json
        esp_timer
        esp_driver_gpio
        esp_driver_uart
)
