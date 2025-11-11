#include "wifi_manager.h"
#include <cstring>

static const char* TAG = "WiFiManager";
#define WIFI_CONNECTED_BIT BIT0

WiFiManager::WiFiManager(const std::string& ssid, const std::string& password)
    : ssid_(ssid), password_(password), connected_(false)
{
    eventGroup_ = xEventGroupCreate();
}

WiFiManager::~WiFiManager() {
    if (eventGroup_) {
        vEventGroupDelete(eventGroup_);
    }
}

void WiFiManager::eventHandler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    auto* self = static_cast<WiFiManager*>(arg);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGW(TAG, "Disconnected, retrying...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(self->eventGroup_, WIFI_CONNECTED_BIT);
        self->connected_ = true;
    }
}

void WiFiManager::initWiFi() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiManager::eventHandler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WiFiManager::eventHandler, this, nullptr));
}

bool WiFiManager::connect() {
    initWiFi();

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, ssid_.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password_.c_str(), sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", ssid_.c_str());

    EventBits_t bits = xEventGroupWaitBits(
        eventGroup_, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    connected_ = (bits & WIFI_CONNECTED_BIT);
    return connected_;
}

bool WiFiManager::isConnected() const {
    return connected_;
}
