// main/config_manager.cpp
#include "config_manager.h"
#include <esp_log.h>
#include <cJSON.h>
#include <cstring>
#include "esp_spiffs.h"

static const char* TAG = "ConfigManager";

bool ConfigManager::init() {
    ESP_LOGI(TAG, "📂 Mounting SPIFFS...");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",  // optional if your partition is labeled "storage"
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return false;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(conf.partition_label, &total, &used);
    ESP_LOGI(TAG, "✅ SPIFFS mounted: total=%d bytes, used=%d bytes", total, used);

    return true;
}

bool ConfigManager::load(const char* path, X402Config& cfg) {
    FILE* f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "❌ Config file not found: %s", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* data = (char*)malloc(len + 1);
    fread(data, 1, len, f);
    data[len] = '\0';
    fclose(f);

    cJSON* root = cJSON_Parse(data);
    free(data);
    if (!root) {
        ESP_LOGE(TAG, "❌ Failed to parse JSON config");
        return false;
    }

    #define GET_STR(field, key) \
        do { cJSON* v = cJSON_GetObjectItem(root, key); \
        if (v && cJSON_IsString(v)) cfg.field = strdup(v->valuestring); } while(0)

    GET_STR(wifi_ssid, "wifi_ssid");
    GET_STR(wifi_password, "wifi_password");
    GET_STR(payai_url, "payai_url");
    GET_STR(solana_rpc_url, "solana_rpc_url");
    GET_STR(user_agent, "user_agent");
    GET_STR(token_mint, "token_mint");

    cJSON* dec = cJSON_GetObjectItem(root, "token_decimals");
    if (dec && cJSON_IsNumber(dec)) cfg.token_decimals = dec->valueint;

    // Load 32-byte keys
    auto load_bytes = [](uint8_t* dest, cJSON* arr) {
        if (!arr || !cJSON_IsArray(arr) || cJSON_GetArraySize(arr) != 32) return false;
        for (int i = 0; i < 32; i++)
            dest[i] = (uint8_t)cJSON_GetArrayItem(arr, i)->valueint;
        return true;
    };
    cJSON* priv = cJSON_GetObjectItem(root, "payer_private_key");
    cJSON* pub = cJSON_GetObjectItem(root, "payer_public_key");
    load_bytes(cfg.payer_private_key, priv);
    load_bytes(cfg.payer_public_key, pub);

    cJSON_Delete(root);
    ESP_LOGI(TAG, "✅ Configuration loaded successfully from %s", path);
    return true;
}
