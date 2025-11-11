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

    // Create and run the payment client
    X402PaymentClient client(config);
    bool success = client.run();

    if (success) {
        ESP_LOGI(TAG, "✅ Payment flow completed successfully");
    } else {
        ESP_LOGE(TAG, "❌ Payment flow failed");
    }

    // Keep running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}