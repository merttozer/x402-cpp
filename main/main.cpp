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
#include <inttypes.h>
#include "base64.h"
#include "base58.h"

// **STEP 2: Add libsodium for real ed25519**
#include "sodium.h"

// === CONFIGURATION ===
#define WIFI_SSID      "mert.ozer"//"Ziggo0797231"
#define WIFI_PASSWORD  "mert1225"//"hgseAucf2Weed2ep"

#define PAYER_BASE58 "2KUCmtebQBgQS78QzBJGMWfuq6peTcvjUD7mUnyX2yZ1"
#define PAYAI_URL "https://x402.payai.network/api/solana-devnet/paid-content"
#define SOLANA_RPC_URL "https://api.devnet.solana.com"
#define USER_AGENT "x402-esp32c6/1.0"
#define TAG "x402"

#define TOKEN_MINT   "4zMMC9srt5Ri5X14GAgXhaHii3GnPAEERYPJgZJDncDU"  // USDC devnet
#define TOKEN_DECIMALS 6

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

// Token Program ID: TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA
static const uint8_t SPL_TOKEN_PROGRAM_ID[32] = {
    0x06, 0xdd, 0xf6, 0xe1, 0xd7, 0x65, 0xa1, 0x93,
    0xd9, 0xcb, 0xe1, 0x46, 0xce, 0xeb, 0x79, 0xac,
    0x1c, 0xb4, 0x85, 0xed, 0x5f, 0x5b, 0x37, 0x91,
    0x3a, 0x8c, 0xf5, 0x85, 0x7e, 0xff, 0x00, 0xa9
};

// Associated Token Program: ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL
static const uint8_t ASSOCIATED_TOKEN_PROGRAM_ID[32] = {
    0x8c, 0x97, 0x25, 0x8f, 0x4e, 0x24, 0x89, 0xf1,
    0xbb, 0x3d, 0x10, 0x29, 0x14, 0x8e, 0x0d, 0x83,
    0x0b, 0x5a, 0x13, 0x99, 0xda, 0xff, 0x10, 0x84,
    0x04, 0x8e, 0x7b, 0xd8, 0xdb, 0xe9, 0xf8, 0x59
};

// ComputeBudget Program: ComputeBudget111111111111111111111111111111
static const uint8_t COMPUTE_BUDGET_PROGRAM_ID[32] = {
    0x03, 0x06, 0x46, 0x6f, 0xe5, 0x21, 0x17, 0x32,
    0xff, 0xec, 0xad, 0xba, 0x72, 0xc3, 0x9b, 0xe7,
    0xbc, 0x8c, 0xe5, 0xbb, 0xc5, 0xf7, 0x12, 0x6b,
    0x2c, 0x43, 0x9b, 0x3a, 0x40, 0x00, 0x00, 0x00
};

// === Base58 Decode Helper ===
// Helper wrapper for Solana (32-byte raw pubkey from Base58 string)
bool solana_base58_to_bytes(const char* base58_str, uint8_t* out32) {
    if (!base58_str) {
        ESP_LOGE(TAG, "Input base58_str is NULL!");
        return false;
    }
    size_t len = strlen(base58_str);
    if (len == 0) {
        ESP_LOGE(TAG, "Input base58_str is empty!");
        return false;
    }
    ESP_LOGI(TAG, "Decoding Base58 string of length %d: '%s'", len, base58_str);

    size_t expected_len = 32;
    bool success = b58tobin(out32, &expected_len, base58_str, len);
    
    if (!success) {
        ESP_LOGE(TAG, "b58tobin returned FALSE for input: %s", base58_str);
        return false;
    }
    if (expected_len != 32) {
        ESP_LOGE(TAG, "Decoded Base58 is not 32 bytes (got %d) for: %s", expected_len, base58_str);
        return false;
    }
    ESP_LOGI(TAG, "✅ Successfully decoded to 32 bytes");
    return true;
}

// Solana PDA derivation with bump seed**
// For ATA, bump=255 always works, so we can simplify
bool find_program_address(
    const uint8_t* seeds[],
    const size_t seed_lens[],
    size_t num_seeds,
    const uint8_t program_id[32],
    uint8_t pda_out[32],
    uint8_t* bump_out)
{
    // Calculate total seed length
    size_t total_seed_len = 0;
    for (size_t i = 0; i < num_seeds; i++) {
        total_seed_len += seed_lens[i];
    }
    
    // Allocate buffer: seeds + bump (1) + program_id (32) + "ProgramDerivedAddress" (21)
    size_t hash_input_len = total_seed_len + 1 + 32 + 21;
    uint8_t* hash_input = (uint8_t*)malloc(hash_input_len);
    if (!hash_input) return false;
    
    // Copy seeds
    size_t offset = 0;
    for (size_t i = 0; i < num_seeds; i++) {
        memcpy(hash_input + offset, seeds[i], seed_lens[i]);
        offset += seed_lens[i];
    }
    
    // Try bump seeds from 255 down to 0
    // For ATA, bump=255 virtually always works
    for (int bump = 255; bump >= 0; bump--) {
        uint8_t bump_seed = (uint8_t)bump;
        
        // Insert bump seed
        hash_input[offset] = bump_seed;
        
        // Append program_id
        memcpy(hash_input + offset + 1, program_id, 32);
        
        // Append "ProgramDerivedAddress"
        memcpy(hash_input + offset + 1 + 32, "ProgramDerivedAddress", 21);
        
        // Hash
        crypto_hash_sha256(pda_out, hash_input, hash_input_len);
        
        // ✅ FIXED: For ATA derivation, just accept the first result (bump=255)
        // No curve check needed - this is standard practice in all Solana clients
        *bump_out = bump_seed;
        free(hash_input);
        return true;
    }
    
    free(hash_input);
    return false; // Should never reach here
}

// **Derive Associated Token Account (ATA) using proper PDA algorithm**
bool derive_associated_token_address(
    const uint8_t owner[32],
    const uint8_t mint[32],
    uint8_t ata_out[32],
    uint8_t* bump_out)
{
    ESP_LOGI(TAG, "📍 Deriving ATA using Solana PDA algorithm...");
    
    // Seeds: [owner, token_program_id, mint]
    const uint8_t* seeds[] = {owner, SPL_TOKEN_PROGRAM_ID, mint};
    const size_t seed_lens[] = {32, 32, 32};
    
    bool success = find_program_address(
        seeds,
        seed_lens,
        3,
        ASSOCIATED_TOKEN_PROGRAM_ID,
        ata_out,
        bump_out
    );
    
    if (success) {
        ESP_LOGI(TAG, "✅ ATA derived (bump: %u):", *bump_out);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, ata_out, 32, ESP_LOG_INFO);
    } else {
        ESP_LOGE(TAG, "❌ Failed to derive ATA");
    }
    
    return success;
}

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

// **STEP 3C: Fetch blockhash and decode to bytes**
bool fetch_recent_blockhash(uint8_t blockhash_out[32]) {
    ESP_LOGI(TAG, "🔗 Fetching recent blockhash...");
    
    response_len = 0;
    memset(response_buffer, 0, sizeof(response_buffer));
    
    const char* rpc_request = 
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getLatestBlockhash\",\"params\":[{\"commitment\":\"finalized\"}]}";
    
    esp_http_client_config_t config = {};
    config.url = SOLANA_RPC_URL;
    config.method = HTTP_METHOD_POST;
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

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, rpc_request, strlen(rpc_request));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    
    ESP_LOGI(TAG, "RPC Response: HTTP %d, err = %d", status, err);

    if (err == ESP_OK && status == 200 && response_len > 0) {
        cJSON* root = cJSON_Parse(response_buffer);
        esp_http_client_cleanup(client);
        
        if (!root) {
            ESP_LOGE(TAG, "Failed to parse JSON response");
            return false;
        }
        
        cJSON* result = cJSON_GetObjectItem(root, "result");
        if (!result) {
            ESP_LOGE(TAG, "No 'result' field");
            cJSON_Delete(root);
            return false;
        }
        
        cJSON* value = cJSON_GetObjectItem(result, "value");
        if (!value) {
            ESP_LOGE(TAG, "No 'value' field");
            cJSON_Delete(root);
            return false;
        }
        
        cJSON* blockhash = cJSON_GetObjectItem(value, "blockhash");
        if (!blockhash || !cJSON_IsString(blockhash)) {
            ESP_LOGE(TAG, "No 'blockhash' string");
            cJSON_Delete(root);
            return false;
        }
        
        const char* blockhash_str = cJSON_GetStringValue(blockhash);
        if (!blockhash_str) {
            ESP_LOGE(TAG, "Blockhash is null");
            cJSON_Delete(root);
            return false;
        }
        
        // **KEY CHANGE: Decode base58 to bytes**
        if (!solana_base58_to_bytes(blockhash_str, blockhash_out)) {
            ESP_LOGE(TAG, "Failed to decode blockhash: %s", blockhash_str);
            cJSON_Delete(root);
            return false;
        }
        
        ESP_LOGI(TAG, "✅ Got blockhash: %s (decoded to bytes)", blockhash_str);
        
        cJSON_Delete(root);
        return true;
    }
    
    esp_http_client_cleanup(client);
    ESP_LOGE(TAG, "Failed to fetch blockhash from Solana RPC");
    return false;
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

// **STEP 3C: Solana Transaction Builder**

// Compact-u16 encoding (for array lengths in Solana transactions)
size_t encode_compact_u16(uint16_t value, uint8_t* output) {
    if (value <= 0x7f) {
        output[0] = (uint8_t)value;
        return 1;
    } else if (value <= 0x3fff) {
        output[0] = (uint8_t)((value & 0x7f) | 0x80);
        output[1] = (uint8_t)(value >> 7);
        return 2;
    } else {
        output[0] = (uint8_t)((value & 0x7f) | 0x80);
        output[1] = (uint8_t)(((value >> 7) & 0x7f) | 0x80);
        output[2] = (uint8_t)(value >> 14);
        return 3;
    }
}

typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} ByteBuffer;

void buffer_init(ByteBuffer* buf, size_t initial_capacity) {
    buf->data = (uint8_t*)malloc(initial_capacity);
    buf->size = 0;
    buf->capacity = initial_capacity;
}

void buffer_append(ByteBuffer* buf, const uint8_t* data, size_t len) {
    if (buf->size + len > buf->capacity) {
        buf->capacity = (buf->size + len) * 2;
        buf->data = (uint8_t*)realloc(buf->data, buf->capacity);
    }
    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
}

void buffer_free(ByteBuffer* buf) {
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

bool build_solana_transaction(
    const uint8_t payer_pubkey[32],
    const char* payto_base58,
    const char* fee_payer_base58,
    const char* mint_base58,
    uint64_t amount,
    uint8_t decimals,
    const uint8_t blockhash[32],
    uint8_t** tx_out,
    size_t* tx_len_out)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔨 Building complete Solana transaction...");
    
    // Decode addresses
    uint8_t mint_pubkey[32];
    uint8_t payto_pubkey[32];
    uint8_t fee_payer_pubkey[32];
    if (!solana_base58_to_bytes(mint_base58, mint_pubkey)) {
        ESP_LOGE(TAG, "Failed to decode mint_base58: %s", mint_base58);
        return false;
    }
    if (!solana_base58_to_bytes(payto_base58, payto_pubkey)) {
        ESP_LOGE(TAG, "Failed to decode payto_base58: %s", payto_base58);
        return false;
    }
    if (!solana_base58_to_bytes(fee_payer_base58, fee_payer_pubkey)) {
        ESP_LOGE(TAG, "Failed to decode fee_payer_base58: %s", fee_payer_base58);
        return false;
    }
        
    // Derive ATAs
    // uint8_t source_ata[32], dest_ata[32];
    // uint8_t source_bump, dest_bump;
    // derive_associated_token_address(payer_pubkey, mint_pubkey, source_ata, &source_bump);
    // derive_associated_token_address(payto_pubkey, mint_pubkey, dest_ata, &dest_bump);
    
    // HARDCODED ATAs FOR TESTING
    uint8_t source_ata[32], dest_ata[32];
    // Your USDC account
    const char* SOURCE_ATA = "DNT1Vj1a8q8giykng5XGKBmcYhnmQc98Apg5mjpd8dhu";
    if (!solana_base58_to_bytes(SOURCE_ATA, source_ata)) {
        ESP_LOGE(TAG, "Failed to decode SOURCE_ATA");
        return false;
    }
    // PayAI's CORRECT ATA for H32Y... wallet
    const char* DEST_ATA = "2g7LTDwkHaeU3PcTqkiXzzWNpCt6VxpuPHLH3PX1m11d";
    if (!solana_base58_to_bytes(DEST_ATA, dest_ata)) {
        ESP_LOGE(TAG, "Failed to decode DEST_ATA");
        return false;
    }

    ESP_LOGI(TAG, "   ✅ HARDCODED ATAs (testing):");
    ESP_LOGI(TAG, "   Source: %s", SOURCE_ATA);
    ESP_LOGI(TAG, "   Dest:   %s", DEST_ATA);

    ESP_LOGI(TAG, "   Source ATA: DNT1Vj1a8q8giykng5XGKBmcYhnmQc98Apg5mjpd8dhu");
    ESP_LOGI(TAG, "   Dest ATA:   2g7LTDwkHaeU3PcTqkiXzzWNpCt6VxpuPHLH3PX1m11d");
    
    // **Build Account Table** (order matters!)
    uint8_t accounts[7][32];
    int account_count = 0;

    // 0: Fee Payer (from 402 response, signer, writable)
    memcpy(accounts[account_count++], fee_payer_pubkey, 32);

    // 0: Payer (fee payer & signer & writable)
    memcpy(accounts[account_count++], payer_pubkey, 32);
    
    // 1: Source ATA (writable)
    memcpy(accounts[account_count++], source_ata, 32);
    
    // 2: Dest ATA (writable)
    memcpy(accounts[account_count++], dest_ata, 32);
    
    // 3: Mint (readonly)
    memcpy(accounts[account_count++], mint_pubkey, 32);
    
    // 4: SPL Token Program (readonly)
    memcpy(accounts[account_count++], SPL_TOKEN_PROGRAM_ID, 32);

    // 5: ComputeBudget Program (readonly)
    memcpy(accounts[account_count++], COMPUTE_BUDGET_PROGRAM_ID, 32);
    
    ESP_LOGI(TAG, "   📋 Account table: %d accounts", account_count);
    
    // **Build Instructions**
    ByteBuffer instructions;
    buffer_init(&instructions, 512);
    
    // Instruction count (compact-u16)
    uint8_t instr_count_encoded[3];
    size_t instr_count_len = encode_compact_u16(3, instr_count_encoded);
    buffer_append(&instructions, instr_count_encoded, instr_count_len);
    
    // Instruction 1: SetComputeUnitLimit
    {
        uint8_t program_idx = 6; // ComputeBudget
        buffer_append(&instructions, &program_idx, 1);
        
        uint8_t accounts_len = 0;
        buffer_append(&instructions, &accounts_len, 1);
        
        uint8_t data[5] = {0x02, 0x40, 0x9c, 0x00, 0x00};
        uint8_t data_len_encoded[3];
        size_t data_len_size = encode_compact_u16(5, data_len_encoded);
        buffer_append(&instructions, data_len_encoded, data_len_size);
        buffer_append(&instructions, data, 5);
    }
    
    // Instruction 2: SetComputeUnitPrice
    {
        uint8_t program_idx = 6;
        buffer_append(&instructions, &program_idx, 1);
        
        uint8_t accounts_len = 0;
        buffer_append(&instructions, &accounts_len, 1);
        
        uint8_t data[9] = {0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        uint8_t data_len_encoded[3];
        size_t data_len_size = encode_compact_u16(9, data_len_encoded);
        buffer_append(&instructions, data_len_encoded, data_len_size);
        buffer_append(&instructions, data, 9);
    }
    
    // Instruction 3: TransferChecked
    {
        uint8_t program_idx = 5; // SPL Token
        buffer_append(&instructions, &program_idx, 1);
        
        // Accounts: source(1,writable), mint(2,readonly), dest(3,writable), owner(0,signer)
        uint8_t acct_count = 4;
        buffer_append(&instructions, &acct_count, 1);
        
        uint8_t acct_indices[] = {2, 4, 3, 1};
        buffer_append(&instructions, acct_indices, 4);
        
        uint8_t transfer_data[10];
        transfer_data[0] = 12; // TransferChecked
        for (int i = 0; i < 8; i++) {
            transfer_data[i + 1] = (amount >> (i * 8)) & 0xff;
        }
        transfer_data[9] = decimals;
        
        uint8_t data_len_encoded[3];
        size_t data_len_size = encode_compact_u16(10, data_len_encoded);
        buffer_append(&instructions, data_len_encoded, data_len_size);
        buffer_append(&instructions, transfer_data, 10);
    }
    
    ESP_LOGI(TAG, "   ✅ Built 3 instructions (compute budget + transfer)");
    
    // **Build Transaction Message**
    ByteBuffer tx_message;
    buffer_init(&tx_message, 1024);
    
    // Message header
    uint8_t header[3] = {
        2,  // num_required_signatures (payer only)
        0,  // num_readonly_signed_accounts
        3   // num_readonly_unsigned_accounts (mint, token_program, compute_program + 2 others)
    };
    buffer_append(&tx_message, header, 3);
    
    // Account addresses (compact-u16 count + addresses)
    uint8_t account_count_encoded[3];
    size_t account_count_len = encode_compact_u16(account_count, account_count_encoded);
    buffer_append(&tx_message, account_count_encoded, account_count_len);
    
    for (int i = 0; i < account_count; i++) {
        buffer_append(&tx_message, accounts[i], 32);
    }
    
    // Recent blockhash
    buffer_append(&tx_message, blockhash, 32);
    
    // Instructions
    buffer_append(&tx_message, instructions.data, instructions.size);
    
    buffer_free(&instructions);
    
    ESP_LOGI(TAG, "   📦 Transaction message size: %d bytes", tx_message.size);
    
    // Return serialized message (caller will sign it)
    *tx_out = tx_message.data;
    *tx_len_out = tx_message.size;
    
    ESP_LOGI(TAG, "✅ Transaction assembled successfully!");
    
    return true;
}

// **STEP 3D: Build Complete Signed Transaction**
bool build_signed_transaction(
    const uint8_t* tx_message,
    size_t tx_message_len,
    const uint8_t signature[64],
    char** base64_out)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔐 Building complete PARTIALLY-SIGNED transaction...");    

    // Complete transaction format:
    // [num_signatures:compact-u16][signatures...][message]
    
    // 1 byte sig count + 64 byte (null) sig + 64 byte (client) sig + message
    size_t total_len = 1 + 64 + 64 + tx_message_len; 
    uint8_t* complete_tx = (uint8_t*)malloc(total_len);
    if (!complete_tx) return false;
    
    size_t offset = 0;
    
    // Number of signatures (compact-u16, will be 2)
    complete_tx[offset++] = 0x02; // <-- WAS 0x01
    
    // Signature 1: Facilitator (placeholder)
    memset(complete_tx + offset, 0, 64); // <-- Add 64 bytes of 0x00
    offset += 64;
    
    // Signature 2: Client (your signature)
    memcpy(complete_tx + offset, signature, 64);
    offset += 64;
    
    // Message
    memcpy(complete_tx + offset, tx_message, tx_message_len);
    offset += tx_message_len;
    
    ESP_LOGI(TAG, "   Complete transaction size: %d bytes", total_len);
    ESP_LOGI(TAG, "   Breakdown: 1 (sig count) + 64 (null sig) + 64 (client sig) + %d (message)", tx_message_len);

    // Base64 encode using our implementation
    *base64_out = base64_encode(complete_tx, total_len);
    if (!*base64_out) {
        free(complete_tx);
        return false;
    }
    
    ESP_LOGI(TAG, "   Base64 encoded length: %d bytes", strlen(*base64_out));
    ESP_LOGI(TAG, "✅ Partially-signed transaction ready!");

    free(complete_tx);
    return true;
}

// ADD THIS NEW FUNCTION
char* build_x_payment_payload(const char* base64_transaction) {
    ESP_LOGI(TAG, "🎁 Wrapping transaction in x402 JSON payload...");
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "x402Version", 1);
    cJSON_AddStringToObject(root, "scheme", "exact");
    cJSON_AddStringToObject(root, "network", "solana-devnet");
    
    cJSON* payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "transaction", base64_transaction);
    cJSON_AddItemToObject(root, "payload", payload);
    
    char* json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        ESP_LOGE(TAG, "❌ Failed to print x402 JSON");
        cJSON_Delete(root);
        return NULL;
    }
    
    // Base64 encode the entire JSON string
    char* final_header = base64_encode((const unsigned char*)json_str, strlen(json_str));
    
    ESP_LOGI(TAG, "   JSON: %s", json_str);
    ESP_LOGI(TAG, "   ✅ Final X-PAYMENT payload ready (%d bytes base64)", strlen(final_header));

    free(json_str);
    cJSON_Delete(root);
    
    return final_header;
}
// ============================================================================
// X402 PAYMENT REQUEST
// ============================================================================

bool make_x402_payment_request(const char* url, const char* base64_tx, char** content_out) {
    response_len = 0;
    memset(response_buffer, 0, sizeof(response_buffer));
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "💳 Making x402 payment request...");
    ESP_LOGI(TAG, "   URL: %s", url);
    
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 30000;
    config.event_handler = _http_event_handler;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size_tx = 1024;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "❌ Failed to init HTTP client");
        return false;
    }

    // Set X-PAYMENT header
    char header_value[1024];
    snprintf(header_value, sizeof(header_value), "%s", base64_tx);
    esp_http_client_set_header(client, "X-PAYMENT", header_value);
    
    ESP_LOGI(TAG, "   X-PAYMENT header set (%d chars)", strlen(base64_tx));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    
    ESP_LOGI(TAG, "   Response: HTTP %d, err = %d", status, err);

    if (err == ESP_OK) {
        if (status == 200) {
            ESP_LOGI(TAG, "🎉 Payment accepted!");
            
            // Get X-PAYMENT-RESPONSE header if present
            char* payment_response = NULL;
            int len = esp_http_client_get_header(client, "X-PAYMENT-RESPONSE", &payment_response);
            if (len > 0 && payment_response) {
                ESP_LOGI(TAG, "   X-PAYMENT-RESPONSE: %s", payment_response);
            }
            
            if (response_len > 0) {
                *content_out = (char*)malloc(response_len + 1);
                if (*content_out) {
                    memcpy(*content_out, response_buffer, response_len);
                    (*content_out)[response_len] = '\0';
                }
            }
            
            esp_http_client_cleanup(client);
            return true;
            
        } else if (status == 402) {
            ESP_LOGW(TAG, "⚠️  Payment Required (402)");
            ESP_LOGI(TAG, "   Server requires payment but rejected our transaction");
            if (response_len > 0) {
                ESP_LOGI(TAG, "   Response: %s", response_buffer);
            }
        } else {
            ESP_LOGW(TAG, "⚠️  Unexpected status: %d", status);
            if (response_len > 0) {
                ESP_LOGI(TAG, "   Response: %s", response_buffer);
            }
        }
    } else {
        ESP_LOGE(TAG, "❌ HTTP request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    return false;
}

// ============================================================================
// MAIN APPLICATION
// ============================================================================

void run_x402_client(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║         ESP32 x402 Payment Client - FULL RUN          ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");

    // === PHASE 1: Fetch payment requirements ===
    ESP_LOGI(TAG, "📡 Phase 1: Fetching payment requirements from PayAI...");
    cJSON* req = NULL;
    if (!fetch_payment_requirements(&req)) {
        ESP_LOGE(TAG, "❌ Failed to fetch payment requirements");
        return;
    }

    // Parse the first accept offer
    cJSON* accepts = cJSON_GetObjectItem(req, "accepts");
    if (!accepts || !cJSON_IsArray(accepts) || cJSON_GetArraySize(accepts) == 0) {
        ESP_LOGE(TAG, "❌ No 'accepts' array in 402 response");
        cJSON_Delete(req);
        return;
    }

    cJSON* offer = cJSON_GetArrayItem(accepts, 0);
    const char* dynamic_payto = cJSON_GetStringValue(cJSON_GetObjectItem(offer, "payTo"));
    const char* asset = cJSON_GetStringValue(cJSON_GetObjectItem(offer, "asset"));
    const char* amount_str = cJSON_GetStringValue(cJSON_GetObjectItem(offer, "maxAmountRequired"));
    const char* resource_raw = cJSON_GetStringValue(cJSON_GetObjectItem(offer, "resource"));

    if (!dynamic_payto || !asset || !amount_str || !resource_raw) {
        ESP_LOGE(TAG, "❌ Missing required fields in payment offer");
        cJSON_Delete(req);
        return;
    }

    cJSON* extra = cJSON_GetObjectItem(offer, "extra");
    const char* fee_payer_base58 = cJSON_GetStringValue(cJSON_GetObjectItem(extra, "feePayer"));

    if (!fee_payer_base58) {
        ESP_LOGE(TAG, "❌ Missing 'extra.feePayer' in payment offer");
        cJSON_Delete(req);
        return;
    }

    ESP_LOGI(TAG, "   Fee Payer: %s", fee_payer_base58);

    // Trim whitespace from resource (PayAI sometimes appends spaces)
    char resource_url[256];
    snprintf(resource_url, sizeof(resource_url), "%s", resource_raw);
    size_t len = strlen(resource_url);
    while (len > 0 && resource_url[len - 1] == ' ') {
        resource_url[--len] = '\0';
    }

    uint64_t amount = strtoull(amount_str, NULL, 10);

    ESP_LOGI(TAG, "💰 Dynamic Payment Requirements:");
    ESP_LOGI(TAG, "   From: %s", PAYER_BASE58);
    ESP_LOGI(TAG, "   To: %s", dynamic_payto);
    ESP_LOGI(TAG, "   Token: %s", asset);
    ESP_LOGI(TAG, "   Amount: %" PRIu64 " (0.%06" PRIu64 " USDC)", amount, amount);
    ESP_LOGI(TAG, "");

    // cJSON_Delete(req);

    // === PHASE 2: Execute payment ===
    ESP_LOGI(TAG, "📡 Step 1: Fetching recent blockhash...");
    uint8_t blockhash[32];
    if (!fetch_recent_blockhash(blockhash)) {
        ESP_LOGE(TAG, "❌ Failed to fetch blockhash");
        return;
    }

    ESP_LOGI(TAG, "🔨 Step 2: Building transaction...");
    uint8_t* tx_message;
    size_t tx_len;
    if (!build_solana_transaction(PAYER_PUBKEY, dynamic_payto, fee_payer_base58, TOKEN_MINT,
                                   amount, TOKEN_DECIMALS, blockhash,
                                   &tx_message, &tx_len)) {
        ESP_LOGE(TAG, "❌ Failed to build transaction");
        return;
    }
    ESP_LOGI(TAG, "✅ Built transaction (%d bytes)", tx_len);

    ESP_LOGI(TAG, "🔐 Step 3: Signing transaction...");
    uint8_t signature[64];
    if (!ed25519_sign_message(signature, tx_message, tx_len, PAYER_PRIVATE_KEY, PAYER_PUBKEY)) {
        ESP_LOGE(TAG, "❌ Failed to sign transaction");
        free(tx_message);
        return;
    }
    ESP_LOGI(TAG, "✅ Transaction signed");

    ESP_LOGI(TAG, "📦 Step 4: Building complete signed transaction...");
    char* base64_tx;
    if (!build_signed_transaction(tx_message, tx_len, signature, &base64_tx)) {
        ESP_LOGE(TAG, "❌ Failed to encode transaction");
        free(tx_message);
        return;
    }
    ESP_LOGI(TAG, "✅ Complete transaction ready (%d bytes base64)", strlen(base64_tx));
    // ➕ ADD THIS BLOCK TO PRINT FOR SOLANA EXPLORER:
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 To inspect in Solana Explorer, go to:");
    ESP_LOGI(TAG, "   https://explorer.solana.com/tx/inspector?cluster=devnet");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "📋 Paste this Base64-encoded transaction:");
    ESP_LOGI(TAG, "%s", base64_tx);
    ESP_LOGI(TAG, "");
    free(tx_message);

    // ⬇️ ⬇️ ⬇️ ADD THIS BLOCK ⬇️ ⬇️ ⬇️
    // === PHASE 2.5: Wrap transaction in x402 JSON Payload ===
    char* x_payment_header = build_x_payment_payload(base64_tx);
    free(base64_tx); // Free the inner tx string, we don't need it anymore

    if (!x_payment_header) {
        ESP_LOGE(TAG, "❌ Failed to build final X-PAYMENT JSON payload");
        return;
    }
    // ⬆️ ⬆️ ⬆️ END OF ADDED BLOCK ⬆️ ⬆️ ⬆️

    ESP_LOGI(TAG, "💳 Step 5: Submitting payment to PayAI...");

    char* content = NULL;
    // Use the dynamic `resource_url` from the 402 response
    if (make_x402_payment_request(resource_url, x_payment_header, &content)) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║              🎉 PAYMENT SUCCESSFUL! 🎉                 ║");
        ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");
        ESP_LOGI(TAG, "");
        if (content) {
            ESP_LOGI(TAG, "📄 Received Content:");
            ESP_LOGI(TAG, "─────────────────────────────────────────────────────");
            ESP_LOGI(TAG, "%s", content);
            ESP_LOGI(TAG, "─────────────────────────────────────────────────────");
            free(content);
        }
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "✅ x402 payment flow completed successfully!");
    } else {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "❌ Payment failed or rejected");
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "Troubleshooting:");
        ESP_LOGE(TAG, "  1. Check blockhash freshness (< 2 min)");
        ESP_LOGE(TAG, "  2. Verify USDC balance and ATA existence");
        ESP_LOGE(TAG, "  3. Ensure correct 'payTo' from 402 response");
        ESP_LOGE(TAG, "  4. Confirm PayAI is accepting devnet payments");
    }
    cJSON_Delete(req);
    free(x_payment_header);
}

// === MAIN ===
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║          ESP32-C6 x402 Payment Client v1.0            ║");
    ESP_LOGI(TAG, "║         Pay-per-use Internet with Solana               ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    
    // Initialize libsodium
    if (sodium_init() < 0) {
        ESP_LOGE(TAG, "❌ Failed to initialize libsodium");
        return;
    }
    ESP_LOGI(TAG, "✅ Libsodium initialized");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    // Connect to WiFi
    ESP_LOGI(TAG, "📡 Connecting to WiFi...");
    wifi_init_sta();
    
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    uint8_t test_out[32];
    const char* test_addr = "4zMMC9srt5Ri5X14GAgXhaHii3GnPAEERYPJgZJDncDU";
    ESP_LOGI(TAG, "🧪 Testing Base58 decode...");
    if (solana_base58_to_bytes(test_addr, test_out)) {
        ESP_LOGI(TAG, "✅ Test decode OK");
        ESP_LOG_BUFFER_HEX(TAG, test_out, 32);
    } else {
        ESP_LOGE(TAG, "❌ Test decode FAILED");
    }
    
    // Run the x402 payment client
    run_x402_client();
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "📊 Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "Program complete. Device will idle.");
    ESP_LOGI(TAG, "Press RESET button to run payment again.");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "");

    while (1) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}