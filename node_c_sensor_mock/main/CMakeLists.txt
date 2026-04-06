idf_component_register(
    SRCS
        "main.c"
        "dht11.c"
        "../../common/oled_ssd1306.c"
    INCLUDE_DIRS "." "../../common"
    PRIV_REQUIRES
        nvs_flash
        esp_wifi
        esp_hw_support
        esp_event
        esp_netif
        esp_timer
        driver
)
