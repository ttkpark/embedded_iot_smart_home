idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "../../common"
    PRIV_REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_timer
        driver
)
