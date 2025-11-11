# components/x402_protocol/CMakeLists.txt
idf_component_register(
    SRCS
        "src/crypto_utils.cpp"
        "src/http_client.cpp"
        "src/solana_client.cpp"
        "src/wifi_manager.cpp"
        "src/x402_client.cpp"
        "src/config_manager.cpp"
        "src/display_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_wifi
        esp_event
        nvs_flash
        esp_http_client
        esp-tls
        mbedtls
        libsodium
        json
        log
        lvgl
        esp_lvgl_port
        espressif__esp_lcd_touch_cst816s
    PRIV_REQUIRES
        spiffs
)