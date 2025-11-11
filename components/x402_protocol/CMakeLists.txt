# components/x402_protocol/CMakeLists.txt
idf_component_register(
    SRCS
        "src/crypto_utils.cpp"
        "src/http_client.cpp"
        "src/solana_client.cpp"
        "src/wifi_manager.cpp"
        "src/x402_client.cpp"
        "src/config_manager.cpp"
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
    PRIV_REQUIRES
        spiffs
)