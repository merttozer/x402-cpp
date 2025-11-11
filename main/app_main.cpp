#include <stdio.h>
#include "esp_log.h"
#include "x402_client.h"
#include "config_manager.h"

static const char *TAG = "main";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "🚀 Starting ESP32-C6 X402 Payment Client");

    // Initialize SPIFFS for config storage
    if (!ConfigManager::init()) {
        ESP_LOGE(TAG, "❌ SPIFFS initialization failed, aborting.");
        return;
    }

    // Load configuration from file
    X402Config config = {};
    if (!ConfigManager::load("/spiffs/config.json", config)) {
        ESP_LOGE(TAG, "❌ Failed to load configuration, aborting.");
        return;
    }

    // Create payment client
    X402PaymentClient client(config);
    
    ESP_LOGI(TAG, "🔧 Initializing environment (display, WiFi, crypto, NVS)...");
    // Initialize display and components
    if (!client.init()) {
        ESP_LOGE(TAG, "❌ Client initialization failed");
        return;
    }

    ESP_LOGI(TAG, "✅ Environment ready - entering event loop");
    
    // Run event loop (blocking - shows idle screen and handles button presses)
    client.runEventLoop();
}