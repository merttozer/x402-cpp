#include <stdio.h>
#include <string.h>
#include <cstring>
#include <time.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <esp_http_client.h>
#include <esp_netif.h>
#include <esp_crt_bundle.h>
#include <cJSON.h>
#include "base64.h"

// **STEP 2: Add libsodium for real ed25519**
#include "sodium.h"

// === CONFIGURATION ===
#define WIFI_SSID      "Ziggo0797231"
#define WIFI_PASSWORD  "hgseAucf2Weed2ep"

// Your actual Solana wallet public key (32 bytes)
static const uint8_t PAYER_PUBKEY[32] = {
    19, 150, 230, 103, 171, 26, 139, 89, 106, 149, 136, 156, 95, 249, 178, 216, 
    216, 184, 115, 152, 228, 69, 254, 35, 205, 140, 70, 17, 107, 237, 136, 232
};

// Your actual Solana wallet private key (32 bytes seed)
static const uint8_t PAYER_PRIVATE_KEY[32] = {
    125, 51, 35, 185, 133, 139, 66, 198, 167, 242, 211, 122, 196, 159, 217, 97, 
    150, 41, 194, 128, 66, 100, 125, 114, 60, 164, 25, 245, 150, 187, 159, 32
};

#define PAYER_BASE58 "2KUCmtebQBgQS78QzBJGMWfuq6peTcvjUD7mUnyX2yZ1"

// === CONSTANTS ===
#define PAYAI_URL "https://x402.payai.network/api/solana-devnet/paid-content"
#define USER_AGENT "x402-esp32c6/1.0"
#define TAG "x402"

// === WiFi Event Handling ===
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected, retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID:%s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "Failed to connect to SSID:%s", WIFI_SSID);
    }
}

// === HTTP Response Buffer ===
static char response_buffer[4096];
static int response_len = 0;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            response_len = 0;
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HEADER=%s:%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (response_len + evt->data_len < sizeof(response_buffer)) {
                memcpy(response_buffer + response_len, evt->data, evt->data_len);
                response_len += evt->data_len;
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            response_buffer[response_len] = '\0';
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

// **STEP 2: Real ED25519 Signing with libsodium**
bool ed25519_sign_message(uint8_t signature[64], const uint8_t* message, size_t message_len, 
                          const uint8_t secret_key[32], const uint8_t public_key[32])
{
    ESP_LOGI(TAG, "🔐 Signing message (%d bytes) with ed25519 (libsodium)...", message_len);
    
    // libsodium requires a 64-byte "secret key" which is: [32-byte seed || 32-byte public key]
    uint8_t libsodium_secret_key[64];
    memcpy(libsodium_secret_key, secret_key, 32);      // First 32 bytes: seed
    memcpy(libsodium_secret_key + 32, public_key, 32); // Last 32 bytes: public key
    
    // Sign the message using libsodium's ed25519
    unsigned long long sig_len = 0;
    int result = crypto_sign_detached(
        signature,           // Output: 64-byte signature
        &sig_len,           // Output: signature length (will be 64)
        message,            // Input: message to sign
        message_len,        // Input: message length
        libsodium_secret_key // Input: 64-byte secret key
    );
    
    if (result != 0 || sig_len != 64) {
        ESP_LOGE(TAG, "❌ ED25519 signing failed! Result: %d, sig_len: %llu", result, sig_len);
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Generated REAL ed25519 signature (64 bytes)");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, signature, 64, ESP_LOG_INFO);
    
    return true;
}

// **STEP 2: Verify ED25519 signature**
bool ed25519_verify_signature(const uint8_t signature[64], const uint8_t* message, 
                              size_t message_len, const uint8_t public_key[32])
{
    ESP_LOGI(TAG, "🔍 Verifying signature with ed25519...");
    
    int result = crypto_sign_verify_detached(
        signature,      // Input: 64-byte signature
        message,        // Input: message
        message_len,    // Input: message length
        public_key      // Input: 32-byte public key
    );
    
    if (result == 0) {
        ESP_LOGI(TAG, "✅ Signature verification PASSED!");
        return true;
    } else {
        ESP_LOGE(TAG, "❌ Signature verification FAILED!");
        return false;
    }
}

// **STEP 2: Test real ED25519 signing and verification**
void test_ed25519_signing(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "   STEP 2: Testing REAL ED25519 Signing");
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "");
    
    // Test 1: Sign a message
    const char* test_msg = "Hello Solana from ESP32-C6!";
    uint8_t signature[64];
    
    ESP_LOGI(TAG, "Wallet Public Key:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, PAYER_PUBKEY, 32, ESP_LOG_INFO);
    
    ESP_LOGI(TAG, "Test Message: \"%s\"", test_msg);
    ESP_LOGI(TAG, "");
    
    // Sign the message
    bool sign_success = ed25519_sign_message(
        signature, 
        (const uint8_t*)test_msg, 
        strlen(test_msg),
        PAYER_PRIVATE_KEY,
        PAYER_PUBKEY
    );
    
    if (!sign_success) {
        ESP_LOGE(TAG, "❌ STEP 2 FAILED: Could not sign message");
        return;
    }
    
    ESP_LOGI(TAG, "");
    
    // Verify the signature
    bool verify_success = ed25519_verify_signature(
        signature,
        (const uint8_t*)test_msg,
        strlen(test_msg),
        PAYER_PUBKEY
    );
    
    ESP_LOGI(TAG, "");
    
    if (sign_success && verify_success) {
        ESP_LOGI(TAG, "🎉 STEP 2 TEST PASSED!");
        ESP_LOGI(TAG, "   ✅ Real ed25519 signature generated");
        ESP_LOGI(TAG, "   ✅ Signature verified successfully");
        ESP_LOGI(TAG, "   ✅ Ready for Solana transaction signing!");
    } else {
        ESP_LOGE(TAG, "❌ STEP 2 TEST FAILED");
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "");
}

bool fetch_payment_requirements(cJSON** out_req) {
    response_len = 0;
    memset(response_buffer, 0, sizeof(response_buffer));
    
    esp_http_client_config_t config = {};
    config.url = PAYAI_URL;
    config.method = HTTP_METHOD_GET;
    config.user_agent = USER_AGENT;
    config.timeout_ms = 15000;
    config.event_handler = _http_event_handler;
    config.buffer_size = 2048;
    config.buffer_size_tx = 1024;
    config.skip_cert_common_name_check = true;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    
    ESP_LOGI(TAG, "HTTP GET Status = %d, err = %d", status, err);

    if (err == ESP_OK && status == 402 && response_len > 0) {
        ESP_LOGI(TAG, "Got 402 response: %s", response_buffer);
        cJSON* root = cJSON_Parse(response_buffer);
        esp_http_client_cleanup(client);
        
        if (root && cJSON_GetObjectItem(root, "accepts")) {
            *out_req = root;
            return true;
        }
        if (root) cJSON_Delete(root);
    }
    
    esp_http_client_cleanup(client);
    ESP_LOGE(TAG, "Failed to fetch payment requirements");
    return false;
}

char* build_x_payment_header(const char* pay_to, const char* asset, uint64_t amount) {
    ESP_LOGI(TAG, "Building payment: %llu lamports to %s", amount, pay_to);
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "x402Version", 1);
    cJSON_AddStringToObject(root, "scheme", "exact");
    cJSON_AddStringToObject(root, "network", "solana-devnet");
    
    cJSON* payload = cJSON_CreateObject();
    const char* placeholder_tx = "BASE64_ENCODED_SIGNED_TRANSACTION_PLACEHOLDER";
    cJSON_AddStringToObject(payload, "transaction", placeholder_tx);
    
    cJSON_AddItemToObject(root, "payload", payload);
    
    char* json_str = cJSON_PrintUnformatted(root);
    char* encoded = base64_encode((const unsigned char*)json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(root);
    
    return encoded;
}

bool submit_with_payment(const char* b64_payment) {
    response_len = 0;
    memset(response_buffer, 0, sizeof(response_buffer));
    
    esp_http_client_config_t config = {};
    config.url = PAYAI_URL;
    config.method = HTTP_METHOD_GET;
    config.user_agent = USER_AGENT;
    config.timeout_ms = 20000;
    config.event_handler = _http_event_handler;
    config.buffer_size = 2048;
    config.buffer_size_tx = 1024;
    config.skip_cert_common_name_check = true;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }
    
    esp_http_client_set_header(client, "X-PAYMENT", b64_payment);
    
    ESP_LOGI(TAG, "Submitting payment with X-PAYMENT header (length: %d)", strlen(b64_payment));
    
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    
    ESP_LOGI(TAG, "Payment submission: status=%d, err=%d", status, err);

    if (err == ESP_OK && status == 200 && response_len > 0) {
        ESP_LOGI(TAG, "✅ SUCCESS: %s", response_buffer);
        esp_http_client_cleanup(client);
        return true;
    } else {
        ESP_LOGE(TAG, "❌ Payment failed. HTTP %d, Response: %s", status, response_buffer);
        esp_http_client_cleanup(client);
        return false;
    }
}

// === MAIN ===
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   ESP32-C6 x402 Client - Step 2       ║");
    ESP_LOGI(TAG, "║   Real ED25519 with libsodium          ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Wallet: %s", PAYER_BASE58);
    
    // Initialize libsodium
    if (sodium_init() < 0) {
        ESP_LOGE(TAG, "❌ Failed to initialize libsodium!");
        return;
    }
    ESP_LOGI(TAG, "✅ libsodium initialized successfully");
    ESP_LOGI(TAG, "");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi
    wifi_init_sta();
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    // **STEP 2: Test real ED25519 signing**
    test_ed25519_signing();
    
    // Continue with existing flow
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Press Ctrl+C to stop, or wait 10 seconds to continue with payment flow...");
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    
    ESP_LOGI(TAG, "Fetching payment requirements from PayAI Echo...");
    cJSON* req = nullptr;
    if (!fetch_payment_requirements(&req)) {
        ESP_LOGE(TAG, "Failed to get payment requirements");
        return;
    }

    cJSON* accepts = cJSON_GetObjectItem(req, "accepts");
    if (!accepts || !cJSON_IsArray(accepts) || cJSON_GetArraySize(accepts) == 0) {
        ESP_LOGE(TAG, "No payment methods in response");
        cJSON_Delete(req);
        return;
    }

    cJSON* first = cJSON_GetArrayItem(accepts, 0);
    const char* pay_to = cJSON_GetStringValue(cJSON_GetObjectItem(first, "payTo"));
    const char* resource = cJSON_GetStringValue(cJSON_GetObjectItem(first, "resource"));
    const char* amount_str = cJSON_GetStringValue(cJSON_GetObjectItem(first, "maxAmountRequired"));
    const char* asset = cJSON_GetStringValue(cJSON_GetObjectItem(first, "asset"));

    if (!pay_to || !resource || !amount_str || !asset) {
        ESP_LOGE(TAG, "Missing required fields in payment requirements");
        cJSON_Delete(req);
        return;
    }

    uint64_t amount = strtoull(amount_str, nullptr, 10);
    ESP_LOGI(TAG, "Payment required: %llu lamports", amount);
    ESP_LOGI(TAG, "Pay to: %s", pay_to);
    ESP_LOGI(TAG, "Asset: %s", asset);

    ESP_LOGW(TAG, "⚠️  NOTE: Transaction building is still PLACEHOLDER");
    ESP_LOGW(TAG, "    Next: Step 3 will build real Solana transactions");
    
    char* x_payment_b64 = build_x_payment_header(pay_to, asset, amount);
    if (!x_payment_b64) {
        ESP_LOGE(TAG, "Failed to build X-PAYMENT header");
        cJSON_Delete(req);
        return;
    }

    bool success = submit_with_payment(x_payment_b64);
    free(x_payment_b64);
    cJSON_Delete(req);

    if (success) {
        ESP_LOGI(TAG, "🎉 x402 flow completed on ESP32-C6!");
    } else {
        ESP_LOGE(TAG, "💥 x402 flow failed - expected until Step 3 completes");
    }

    ESP_LOGI(TAG, "Test complete. Idling...");
    while (1) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}