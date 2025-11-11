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
}

bool X402PaymentClient::init() {
    ESP_LOGI(TAG, "🔧 [INIT] Initializing environment...");

    // libsodium
    ESP_LOGI(TAG, "🧂 Initializing libsodium cryptography...");
    if (sodium_init() < 0) {
        ESP_LOGE(TAG, "❌ libsodium initialization failed — cryptographic functions unavailable!");
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
    if (!wifi_->connect()) {
        ESP_LOGE(TAG, "❌ WiFi connection failed — please check SSID/password and signal strength.");
        return false;
    }

    ESP_LOGI(TAG, "✅ Environment initialized and connected to network.");
    return true;
}

bool X402PaymentClient::fetchPaymentOffer(cJSON** offer_json) {
    ESP_LOGI(TAG, "🌍 [STEP 1] Requesting payment offer from %s", cfg_.payai_url);
    if (!http_->get_402(cfg_.payai_url, offer_json)) {
        ESP_LOGE(TAG, "❌ Failed to fetch 402 payment offer — network or server error.");
        return false;
    }
    ESP_LOGI(TAG, "✅ Payment offer successfully received and parsed.");
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
    cJSON* accepts = cJSON_GetObjectItem(offer_json, "accepts");
    if (!accepts || !cJSON_IsArray(accepts) || cJSON_GetArraySize(accepts) == 0) {
        ESP_LOGE(TAG, "❌ Invalid 402 response: missing or empty 'accepts' field.");
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
        cJSON_Delete(offer_json);
        return false;
    }

    uint64_t amount = strtoull(amount_str, nullptr, 10);
    ESP_LOGI(TAG, "💰 [STEP 2] Transaction details — Amount: %.6f %s", (double)amount / 1e6, asset);
    ESP_LOGI(TAG, "   → Pay to: %s", payTo);
    ESP_LOGI(TAG, "   → Fee payer: %s", feePayer);
    ESP_LOGI(TAG, "   → Resource: %s", resource);

    uint8_t blockhash[32];
    ESP_LOGI(TAG, "🔗 [STEP 3] Fetching recent Solana blockhash...");
    if (!solana_->fetchRecentBlockhash(blockhash)) {
        ESP_LOGE(TAG, "❌ Failed to fetch recent blockhash — Solana RPC may be unreachable.");
        cJSON_Delete(offer_json);
        return false;
    }
    ESP_LOGI(TAG, "✅ Recent blockhash successfully obtained.");

    std::vector<uint8_t> tx_message;
    ESP_LOGI(TAG, "🔨 [STEP 4] Building Solana transaction...");
    if (!solana_->buildTransaction(
            cfg_.payer_public_key, payTo, feePayer,
            cfg_.token_mint, amount, cfg_.token_decimals,
            blockhash, tx_message)) {
        ESP_LOGE(TAG, "❌ Failed to construct transaction — token account or encoding error.");
        cJSON_Delete(offer_json);
        return false;
    }
    ESP_LOGI(TAG, "✅ Transaction built successfully (%zu bytes).", tx_message.size());

    uint8_t signature[64];
    ESP_LOGI(TAG, "🔐 [STEP 5] Signing transaction using local private key...");
    if (!CryptoUtils::ed25519Sign(signature,
                                  tx_message.data(),
                                  tx_message.size(),
                                  cfg_.payer_private_key,
                                  cfg_.payer_public_key)) {
        ESP_LOGE(TAG, "❌ Transaction signing failed — check private key integrity.");
        cJSON_Delete(offer_json);
        return false;
    }
    ESP_LOGI(TAG, "✅ Transaction successfully signed with ed25519.");

    std::string base64_tx;
    ESP_LOGI(TAG, "📦 [STEP 6] Encoding signed transaction to Base64...");
    if (!solana_->buildSignedTransaction(tx_message, signature, base64_tx)) {
        ESP_LOGE(TAG, "❌ Could not encode signed transaction — memory or encoding failure.");
        cJSON_Delete(offer_json);
        return false;
    }
    ESP_LOGI(TAG, "✅ Transaction encoded successfully (Base64 length: %zu).", base64_tx.size());

    ESP_LOGI(TAG, "💸 [STEP 7] Submitting payment to PayAI endpoint...");
    char* x_payment_header = buildPaymentPayload(base64_tx.c_str());
    char* content = nullptr;
    bool ok = http_->submit_payment(resource, x_payment_header, &content);

    free(x_payment_header);
    cJSON_Delete(offer_json);

    if (!ok) {
        ESP_LOGE(TAG, "❌ Payment submission failed — check network and server response.");
        return false;
    }

    ESP_LOGI(TAG, "✅ [SUCCESS] Payment successfully completed and confirmed by server!");
    if (content) {
        ESP_LOGI(TAG, "📦 Raw Server Response:\n%s", content);

        // 🧩 Parse premium content field if available
        cJSON* response_json = cJSON_Parse(content);
        if (response_json) {
            cJSON* premium = cJSON_GetObjectItem(response_json, "premiumContent");
            if (premium && cJSON_IsString(premium)) {
                ESP_LOGI(TAG, "💎 Premium Content Unlocked:");
                ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                ESP_LOGI(TAG, "✨ %s", premium->valuestring);
                ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            } else {
                ESP_LOGW(TAG, "⚠️ No 'premiumContent' field in response.");
            }
            cJSON_Delete(response_json);
        } else {
            ESP_LOGW(TAG, "⚠️ Could not parse JSON response.");
        }

        free(content);
    } else {
        ESP_LOGW(TAG, "⚠️ No content returned from server.");
    }

    ESP_LOGI(TAG, "🏁 x402 Payment Client run finished — returning success.");
    return true;
}
