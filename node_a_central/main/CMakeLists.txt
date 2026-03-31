idf_component_register(
    SRCS "main.c" "ble_gatt.c"
    INCLUDE_DIRS "." "../../common"
    PRIV_REQUIRES
        nvs_flash
        esp_wifi
        esp_hw_support
        esp_event
        esp_netif
        esp_http_server
        spiffs
        esp_timer
        json
        driver
        bt
)
