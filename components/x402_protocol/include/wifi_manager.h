#pragma once

#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_netif.h>

class WiFiManager {
public:
    explicit WiFiManager(const std::string& ssid, const std::string& password);
    ~WiFiManager();

    bool connect();          // Connect to WiFi network
    bool isConnected() const; // Check connection state

private:
    static void eventHandler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data);

    void initWiFi();

    std::string ssid_;
    std::string password_;
    EventGroupHandle_t eventGroup_;
    bool connected_;
};
