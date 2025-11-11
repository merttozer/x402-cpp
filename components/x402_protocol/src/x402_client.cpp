#include "x402_client.h"
#include "crypto_utils.h"
#include <esp_log.h>
#include <sodium.h>
#include <cJSON.h>
#include <nvs_flash.h>
#include <cstdlib>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "x402";

char* X402PaymentClient::buildPaymentPayload(const char* base64_tx) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "x402Version", 1);
    cJSON_AddStringToObject(root, "scheme", "exact");
    cJSON_AddStringToObject(root, "network", "solana-devnet");

    cJSON* payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "transaction", base64_tx);
    cJSON_AddItemToObject(root, "payload", payload);

    char* json_str = cJSON_PrintUnformatted(root);
    char* final_header = CryptoUtils::base64Encode(
        reinterpret_cast<const unsigned char*>(json_str),
        strlen(json_str));

    cJSON_Delete(root);
    free(json_str);
    return final_header;
}

X402PaymentClient::X402PaymentClient(const X402Config& config)
    : cfg_(config)
    , env_initialized_(false)
{
    solana_ = std::make_unique<SolanaClient>(cfg_.solana_rpc_url);
    wifi_   = std::make_unique<WiFiManager>(cfg_.wifi_ssid, cfg_.wifi_password);
    http_   = std::make_unique<HttpClient>(HttpClientConfig{cfg_.user_agent, 20000});
    display_ = std::make_unique<DisplayManager>();
}

bool X402PaymentClient::init() {
    if (env_initialized_) {
        ESP_LOGI(TAG, "Environment already initialized");
        return true;
    }

    ESP_LOGI(TAG, "🔧 [INIT] Initializing environment...");

    // Initialize display
    ESP_LOGI(TAG, "Initializing display");

    if (!display_->init()) {
        ESP_LOGE(TAG, "❌ Display initialization failed");
        return false;
    }

    // libsodium
    ESP_LOGI(TAG, "🧂 Initializing libsodium cryptography...");
    
    if (sodium_init() < 0) {
        ESP_LOGE(TAG, "❌ libsodium initialization failed");
        vTaskDelay(pdMS_TO_TICKS(2000));
        return false;
    }
    ESP_LOGI(TAG, "✅ libsodium initialized successfully.");
    vTaskDelay(pdMS_TO_TICKS(500));

    // NVS
    ESP_LOGI(TAG, "💾 Initializing NVS...");
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "⚠️ NVS partition issue detected — erasing...");
        nvs_flash_erase();
        nvs_flash_init();
    }
    ESP_LOGI(TAG, "✅ NVS initialized successfully.");
    vTaskDelay(pdMS_TO_TICKS(500));

    // WiFi
    ESP_LOGI(TAG, "📶 Connecting to WiFi '%s'...", cfg_.wifi_ssid);
    
    // Try to connect with timeout
    int retry_count = 0;
    const int max_retries = 10;  // 10 seconds timeout
    
    if (!wifi_->connect()) {
        ESP_LOGE(TAG, "❌ WiFi connect start failed");
        vTaskDelay(pdMS_TO_TICKS(2000));
        return false;
    }
    
    // Wait for connection with timeout
    while (retry_count < max_retries) {
        if (wifi_->isConnected()) {
            ESP_LOGI(TAG, "✅ WiFi connected!");
            break;
        }
        
        ESP_LOGI(TAG, "Waiting for WiFi... (%d/%d)", retry_count + 1, max_retries);
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry_count++;
    }
    
    if (retry_count >= max_retries) {
        ESP_LOGE(TAG, "❌ WiFi connection timeout");
        vTaskDelay(pdMS_TO_TICKS(2000));
        return false;
    }

    ESP_LOGI(TAG, "✅ Environment initialized.");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    env_initialized_ = true;
    return true;
}

bool X402PaymentClient::fetchPaymentOffer(cJSON** offer_json) {
    ESP_LOGI(TAG, "🌍 [STEP 1] Requesting payment offer...");
    display_->showStatus("Payment", "Fetching offer...");
    
    if (!http_->get_402(cfg_.payai_url, offer_json)) {
        ESP_LOGE(TAG, "❌ Failed to fetch payment offer");
        display_->showError("Offer Fetch\nFailed!");
        vTaskDelay(pdMS_TO_TICKS(2000));
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Payment offer received");
    display_->showStatus("Payment", "Offer received");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    return true;
}

bool X402PaymentClient::executePaymentFlow() {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🚀 Starting payment flow...");

    // Environment should already be initialized from main
    // Just verify WiFi is connected
    if (!env_initialized_) {
        ESP_LOGW(TAG, "Environment not initialized, initializing now...");
        if (!init()) {
            return false;
        }
    }

    cJSON* offer_json = nullptr;
    if (!fetchPaymentOffer(&offer_json)) {
        return false;
    }

    ESP_LOGI(TAG, "🔍 Parsing offer details...");
    display_->showStatus("Payment", "Parsing offer...");
    
    cJSON* accepts = cJSON_GetObjectItem(offer_json, "accepts");
    if (!accepts || !cJSON_IsArray(accepts) || cJSON_GetArraySize(accepts) == 0) {
        ESP_LOGE(TAG, "❌ Invalid offer");
        display_->showError("Invalid\nOffer!");
        cJSON_Delete(offer_json);
        vTaskDelay(pdMS_TO_TICKS(2000));
        return false;
    }

    cJSON* offer = cJSON_GetArrayItem(accepts, 0);
    const char* payTo = cJSON_GetStringValue(cJSON_GetObjectItem(offer, "payTo"));
    const char* asset = cJSON_GetStringValue(cJSON_GetObjectItem(offer, "asset"));
    const char* amount_str = cJSON_GetStringValue(cJSON_GetObjectItem(offer, "maxAmountRequired"));
    const char* resource = cJSON_GetStringValue(cJSON_GetObjectItem(offer, "resource"));
    const char* feePayer = cJSON_GetStringValue(
        cJSON_GetObjectItem(cJSON_GetObjectItem(offer, "extra"), "feePayer"));

    if (!payTo || !asset || !amount_str || !resource || !feePayer) {
        ESP_LOGE(TAG, "❌ Incomplete offer data");
        display_->showError("Invalid\nOffer Data!");
        cJSON_Delete(offer_json);
        vTaskDelay(pdMS_TO_TICKS(2000));
        return false;
    }

    uint64_t amount = strtoull(amount_str, nullptr, 10);
    ESP_LOGI(TAG, "💰 Amount: %.6f %s", (double)amount / 1e6, asset);

    char amount_display[64];
    snprintf(amount_display, sizeof(amount_display), "Amount:\n%.6f", (double)amount / 1e6);
    display_->showStatus("Transaction", amount_display);
    vTaskDelay(pdMS_TO_TICKS(1500));

    uint8_t blockhash[32];
    ESP_LOGI(TAG, "🔗 [STEP 3] Fetching blockhash...");
    display_->showStatus("Solana", "Fetching blockhash...");
    
    if (!solana_->fetchRecentBlockhash(blockhash)) {
        ESP_LOGE(TAG, "❌ Failed to fetch blockhash");
        display_->showError("Blockhash\nFailed!");
        cJSON_Delete(offer_json);
        vTaskDelay(pdMS_TO_TICKS(2000));
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Blockhash obtained");
    display_->showStatus("Solana", "Blockhash OK");
    vTaskDelay(pdMS_TO_TICKS(500));

    std::vector<uint8_t> tx_message;
    ESP_LOGI(TAG, "🔨 [STEP 4] Building transaction...");
    display_->showStatus("Transaction", "Building...");
    
    if (!solana_->buildTransaction(
            cfg_.payer_public_key, payTo, feePayer,
            cfg_.token_mint, amount, cfg_.token_decimals,
            blockhash, tx_message)) {
        ESP_LOGE(TAG, "❌ Failed to build transaction");
        display_->showError("TX Build\nFailed!");
        cJSON_Delete(offer_json);
        vTaskDelay(pdMS_TO_TICKS(2000));
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Transaction built (%zu bytes)", tx_message.size());
    display_->showStatus("Transaction", "Built!");
    vTaskDelay(pdMS_TO_TICKS(500));

    uint8_t signature[64];
    ESP_LOGI(TAG, "🔐 [STEP 5] Signing...");
    display_->showStatus("Signing", "Signing TX...");
    
    if (!CryptoUtils::ed25519Sign(signature,
                                  tx_message.data(),
                                  tx_message.size(),
                                  cfg_.payer_private_key,
                                  cfg_.payer_public_key)) {
        ESP_LOGE(TAG, "❌ Signing failed");
        display_->showError("Signing\nFailed!");
        cJSON_Delete(offer_json);
        vTaskDelay(pdMS_TO_TICKS(2000));
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Transaction signed");
    display_->showStatus("Signing", "Signed!");
    vTaskDelay(pdMS_TO_TICKS(500));

    std::string base64_tx;
    ESP_LOGI(TAG, "📦 [STEP 6] Encoding...");
    display_->showStatus("Encoding", "Encoding...");
    
    if (!solana_->buildSignedTransaction(tx_message, signature, base64_tx)) {
        ESP_LOGE(TAG, "❌ Encoding failed");
        display_->showError("Encoding\nFailed!");
        cJSON_Delete(offer_json);
        vTaskDelay(pdMS_TO_TICKS(2000));
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Transaction encoded");
    display_->showStatus("Encoding", "Encoded!");
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "💸 [STEP 7] Submitting payment...");
    display_->showStatus("Payment", "Submitting...");
    
    char* x_payment_header = buildPaymentPayload(base64_tx.c_str());
    char* content = nullptr;
    bool ok = http_->submit_payment(resource, x_payment_header, &content);

    free(x_payment_header);
    cJSON_Delete(offer_json);

    if (!ok) {
        ESP_LOGE(TAG, "❌ Payment submission failed");
        display_->showError("Payment\nFailed!");
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    }

    ESP_LOGI(TAG, "✅ [SUCCESS] Payment completed!");
    
    if (content) {
        ESP_LOGI(TAG, "📦 Response:\n%s", content);

        cJSON* response_json = cJSON_Parse(content);
        if (response_json) {
            cJSON* premium = cJSON_GetObjectItem(response_json, "premiumContent");
            if (premium && cJSON_IsString(premium)) {
                ESP_LOGI(TAG, "💎 Premium: %s", premium->valuestring);
                display_->showSuccess(premium->valuestring);
            } else {
                display_->showSuccess("Payment\nSuccessful!");
            }
            cJSON_Delete(response_json);
        } else {
            display_->showSuccess("Payment\nSuccessful!");
        }

        free(content);
    } else {
        display_->showSuccess("Payment\nSuccessful!");
    }

    ESP_LOGI(TAG, "🏁 Payment flow finished");
    return true;
}

// Static task wrapper
static void paymentTaskWrapper(void* arg) {
    X402PaymentClient* client = static_cast<X402PaymentClient*>(arg);
    
    ESP_LOGI(TAG, "💡 Payment task started");
    
    bool success = client->executePaymentFlow();
    
    // Return to idle after showing result
    if (success) {
        client->returnToIdleAfterDelay(5000);  // 5 seconds
    } else {
        client->returnToIdleAfterDelay(3000);  // 3 seconds
    }
    
    ESP_LOGI(TAG, "💡 Payment task finished");
    
    // Delete task
    vTaskDelete(NULL);
}

void X402PaymentClient::onPaymentButtonPressed() {
    ESP_LOGI(TAG, "💡 Payment button pressed - creating payment task");
    
    // Create task with large stack for network operations
    BaseType_t ret = xTaskCreate(
        paymentTaskWrapper,
        "payment_task",
        8192,  // 8KB stack
        this,
        5,     // Priority
        NULL
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create payment task");
        display_->showError("Task\nCreation\nFailed!");
        returnToIdleAfterDelay(3000);
    }
}

void X402PaymentClient::returnToIdleAfterDelay(uint32_t delay_ms) {
    ESP_LOGI(TAG, "Returning to idle in %lu ms", delay_ms);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    
    // Show idle screen again
    display_->showIdleScreen([this]() {
        this->onPaymentButtonPressed();
    });
}

void X402PaymentClient::runEventLoop() {
    ESP_LOGI(TAG, "🔄 Starting event loop");
    
    // Show initial idle screen
    display_->showIdleScreen([this]() {
        this->onPaymentButtonPressed();
    });
    
    // Keep running forever
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}