idf_component_register(
    SRCS "main.cpp" "base64.c" "base58.c"
    INCLUDE_DIRS "."
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
)