#include "x402_client.h"
#include "crypto_utils.h"
#include <esp_log.h>
#include <sodium.h>
#include <cJSON.h>
#include <nvs_flash.h>
#include <cstdlib>
#include <cstring>

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
{
    solana_ = std::make_unique<SolanaClient>(cfg_.solana_rpc_url);
    wifi_   = std::make_unique<WiFiManager>(cfg_.wifi_ssid, cfg_.wifi_password);
    http_   = std::make_unique<HttpClient>(HttpClientConfig{cfg_.user_agent, 20000});
    display_ = std::make_unique<DisplayManager>();
}

bool X402PaymentClient::init() {
    ESP_LOGI(TAG, "🔧 [INIT] Initializing environment...");

    // Initialize display
    if (!display_->init()) {
        ESP_LOGE(TAG, "❌ Display initialization failed");
        return false;
    }

    // Initialize display first
    display_->showStatus("X402 Client", "Initializing...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // libsodium
    ESP_LOGI(TAG, "🧂 Initializing libsodium cryptography...");
    
    if (sodium_init() < 0) {
        ESP_LOGE(TAG, "❌ libsodium initialization failed — cryptographic functions unavailable!");
        display_->showError("Crypto Init\nFailed!");
        return false;
    }
    ESP_LOGI(TAG, "✅ libsodium initialized successfully.");

    // NVS
    ESP_LOGI(TAG, "💾 Initializing NVS (non-volatile storage)...");
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "⚠️ NVS partition issue detected — erasing and reinitializing...");
        nvs_flash_erase();
        nvs_flash_init();
    }
    ESP_LOGI(TAG, "✅ NVS initialized successfully.");

    // WiFi
    ESP_LOGI(TAG, "📶 Connecting to WiFi network '%s'...", cfg_.wifi_ssid);
    display_->showStatus("WiFi", "Connecting...");
    
    if (!wifi_->connect()) {
        ESP_LOGE(TAG, "❌ WiFi connection failed — please check SSID/password and signal strength.");
        display_->showError("WiFi\nConnection\nFailed!");
        return false;
    }

    ESP_LOGI(TAG, "✅ Environment initialized and connected to network.");
    display_->showStatus("WiFi", "Connected!");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    return true;
}

bool X402PaymentClient::fetchPaymentOffer(cJSON** offer_json) {
    ESP_LOGI(TAG, "🌍 [STEP 1] Requesting payment offer from %s", cfg_.payai_url);
    display_->showStatus("Payment", "Fetching offer...");
    
    if (!http_->get_402(cfg_.payai_url, offer_json)) {
        ESP_LOGE(TAG, "❌ Failed to fetch 402 payment offer — network or server error.");
        display_->showError("Offer Fetch\nFailed!");
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Payment offer successfully received and parsed.");
    display_->showStatus("Payment", "Offer received");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    return true;
}

bool X402PaymentClient::run() {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🚀 Starting x402 Payment Client (Solana Devnet)");

    if (!init()) {
        ESP_LOGE(TAG, "❌ Initialization phase failed — aborting payment process.");
        return false;
    }

    cJSON* offer_json = nullptr;
    if (!fetchPaymentOffer(&offer_json)) return false;

    ESP_LOGI(TAG, "🔍 Parsing 402 offer details...");
    display_->showStatus("Payment", "Parsing offer...");
    
    cJSON* accepts = cJSON_GetObjectItem(offer_json, "accepts");
    if (!accepts || !cJSON_IsArray(accepts) || cJSON_GetArraySize(accepts) == 0) {
        ESP_LOGE(TAG, "❌ Invalid 402 response: missing or empty 'accepts' field.");
        display_->showError("Invalid\nOffer!");
        cJSON_Delete(offer_json);
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
        ESP_LOGE(TAG, "❌ Offer missing required fields (payTo, asset, amount, resource, feePayer).");
        display_->showError("Invalid\nOffer Data!");
        cJSON_Delete(offer_json);
        return false;
    }

    uint64_t amount = strtoull(amount_str, nullptr, 10);
    ESP_LOGI(TAG, "💰 [STEP 2] Transaction details — Amount: %.6f %s", (double)amount / 1e6, asset);
    ESP_LOGI(TAG, "   → Pay to: %s", payTo);
    ESP_LOGI(TAG, "   → Fee payer: %s", feePayer);
    ESP_LOGI(TAG, "   → Resource: %s", resource);

    char amount_display[64];
    snprintf(amount_display, sizeof(amount_display), "Amount:\n%.6f", (double)amount / 1e6);
    display_->showStatus("Transaction", amount_display);
    vTaskDelay(pdMS_TO_TICKS(1500));

    uint8_t blockhash[32];
    ESP_LOGI(TAG, "🔗 [STEP 3] Fetching recent Solana blockhash...");
    display_->showStatus("Solana", "Fetching blockhash...");
    
    if (!solana_->fetchRecentBlockhash(blockhash)) {
        ESP_LOGE(TAG, "❌ Failed to fetch recent blockhash — Solana RPC may be unreachable.");
        display_->showError("Blockhash\nFetch Failed!");
        cJSON_Delete(offer_json);
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Recent blockhash successfully obtained.");
    display_->showStatus("Solana", "Blockhash OK");
    vTaskDelay(pdMS_TO_TICKS(500));

    std::vector<uint8_t> tx_message;
    ESP_LOGI(TAG, "🔨 [STEP 4] Building Solana transaction...");
    display_->showStatus("Transaction", "Building...");
    
    if (!solana_->buildTransaction(
            cfg_.payer_public_key, payTo, feePayer,
            cfg_.token_mint, amount, cfg_.token_decimals,
            blockhash, tx_message)) {
        ESP_LOGE(TAG, "❌ Failed to construct transaction — token account or encoding error.");
        display_->showError("TX Build\nFailed!");
        cJSON_Delete(offer_json);
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Transaction built successfully (%zu bytes).", tx_message.size());
    display_->showStatus("Transaction", "Built successfully");
    vTaskDelay(pdMS_TO_TICKS(500));

    uint8_t signature[64];
    ESP_LOGI(TAG, "🔐 [STEP 5] Signing transaction using local private key...");
    display_->showStatus("Signing", "Signing TX...");
    
    if (!CryptoUtils::ed25519Sign(signature,
                                  tx_message.data(),
                                  tx_message.size(),
                                  cfg_.payer_private_key,
                                  cfg_.payer_public_key)) {
        ESP_LOGE(TAG, "❌ Transaction signing failed — check private key integrity.");
        display_->showError("Signing\nFailed!");
        cJSON_Delete(offer_json);
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Transaction successfully signed with ed25519.");
    display_->showStatus("Signing", "TX Signed");
    vTaskDelay(pdMS_TO_TICKS(500));

    std::string base64_tx;
    ESP_LOGI(TAG, "📦 [STEP 6] Encoding signed transaction to Base64...");
    display_->showStatus("Encoding", "Encoding TX...");
    
    if (!solana_->buildSignedTransaction(tx_message, signature, base64_tx)) {
        ESP_LOGE(TAG, "❌ Could not encode signed transaction — memory or encoding failure.");
        display_->showError("Encoding\nFailed!");
        cJSON_Delete(offer_json);
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Transaction encoded successfully (Base64 length: %zu).", base64_tx.size());
    display_->showStatus("Encoding", "TX Encoded");
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "💸 [STEP 7] Submitting payment to PayAI endpoint...");
    display_->showStatus("Payment", "Submitting...");
    
    char* x_payment_header = buildPaymentPayload(base64_tx.c_str());
    char* content = nullptr;
    bool ok = http_->submit_payment(resource, x_payment_header, &content);

    free(x_payment_header);
    cJSON_Delete(offer_json);

    if (!ok) {
        ESP_LOGE(TAG, "❌ Payment submission failed — check network and server response.");
        display_->showError("Payment\nSubmission\nFailed!");
        return false;
    }

    ESP_LOGI(TAG, "✅ [SUCCESS] Payment successfully completed and confirmed by server!");
    
    if (content) {
        ESP_LOGI(TAG, "📦 Raw Server Response:\n%s", content);

        // Parse premium content field if available
        cJSON* response_json = cJSON_Parse(content);
        if (response_json) {
            cJSON* premium_item = cJSON_GetObjectItem(response_json, "premiumContent");
            cJSON* tx_hash_item = cJSON_GetObjectItem(response_json, "signature");
            if (premium_item && cJSON_IsString(premium_item) && tx_hash_item && cJSON_IsString(tx_hash_item)) {
                ESP_LOGI(TAG, "💎 Premium Content Unlocked:");
                ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                ESP_LOGI(TAG, "✨ %s", premium_item->valuestring);
                ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                
                char display_content[256] = {0};
                const char* premium_str = premium_item->valuestring;
                const char* tx_hash_str = tx_hash_item->valuestring;

                // Show premium content on display
                snprintf(display_content, sizeof(display_content) - 1, 
                        "%s\nTX Hash:\n%s",
                        premium_str, 
                        tx_hash_str);
                display_->showSuccess(display_content);
            } else {
                ESP_LOGW(TAG, "⚠️ No 'premiumContent' field in response.");
                display_->showSuccess("Payment\nSuccessful!");
            }
            cJSON_Delete(response_json);
        } else {
            ESP_LOGW(TAG, "⚠️ Could not parse JSON response.");
            display_->showSuccess("Payment\nSuccessful!");
        }

        free(content);
    } else {
        ESP_LOGW(TAG, "⚠️ No content returned from server.");
        display_->showSuccess("Payment\nSuccessful!");
    }

    ESP_LOGI(TAG, "🏁 x402 Payment Client run finished — returning success.");
    return true;
}