// main/app_main.cpp
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "x402_client.h"  
#include "config_manager.h"

extern "C" void app_main(void) {
    ESP_LOGI("app_main", "🚀 Starting app...");

    if (!ConfigManager::init()) {
        ESP_LOGE("app_main", "❌ SPIFFS init failed, aborting.");
        return;
    }

    X402Config config = {};
    if (!ConfigManager::load("/spiffs/config.json", config)) {
        ESP_LOGE("app_main", "❌ Failed to load configuration, aborting.");
        return;
    }

    X402PaymentClient client(config);
    client.run();

    while (1) vTaskDelay(10000 / portTICK_PERIOD_MS);
}