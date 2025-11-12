#include "solana_client.h"
#include "crypto_utils.h"
#include "http_client.h"

#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cJSON.h>
#include <sodium.h>
#include <cstring>
#include <cstdlib>

static const char* TAG = "SolanaClient";

// === Program IDs ===
const uint8_t SolanaClient::SPL_TOKEN_PROGRAM_ID[32] = {
    0x06,0xdd,0xf6,0xe1,0xd7,0x65,0xa1,0x93,0xd9,0xcb,0xe1,0x46,0xce,0xeb,0x79,0xac,
    0x1c,0xb4,0x85,0xed,0x5f,0x5b,0x37,0x91,0x3a,0x8c,0xf5,0x85,0x7e,0xff,0x00,0xa9
};

const uint8_t SolanaClient::ASSOCIATED_TOKEN_PROGRAM_ID[32] = {
    0x8c,0x97,0x25,0x8f,0x4e,0x24,0x89,0xf1,0xbb,0x3d,0x10,0x29,0x14,0x8e,0x0d,0x83,
    0x0b,0x5a,0x13,0x99,0xda,0xff,0x10,0x84,0x04,0x8e,0x7b,0xd8,0xdb,0xe9,0xf8,0x59
};

const uint8_t SolanaClient::COMPUTE_BUDGET_PROGRAM_ID[32] = {
    0x03,0x06,0x46,0x6f,0xe5,0x21,0x17,0x32,0xff,0xec,0xad,0xba,0x72,0xc3,0x9b,0xe7,
    0xbc,0x8c,0xe5,0xbb,0xc5,0xf7,0x12,0x6b,0x2c,0x43,0x9b,0x3a,0x40,0x00,0x00,0x00
};

SolanaClient::SolanaClient(const std::string& rpcUrl)
    : rpcUrl_(rpcUrl) {}

// === Helper ===
void SolanaClient::ByteBuffer::append(const void* ptr, size_t len) {
    const uint8_t* bytes = static_cast<const uint8_t*>(ptr);
    data.insert(data.end(), bytes, bytes + len);
}

size_t SolanaClient::encodeCompactU16(uint16_t value, uint8_t* output) {
    if (value <= 0x7f) {
        output[0] = value;
        return 1;
    } else if (value <= 0x3fff) {
        output[0] = (value & 0x7f) | 0x80;
        output[1] = value >> 7;
        return 2;
    } else {
        output[0] = (value & 0x7f) | 0x80;
        output[1] = ((value >> 7) & 0x7f) | 0x80;
        output[2] = value >> 14;
        return 3;
    }
}

// === PDA ===
bool SolanaClient::findProgramAddress(
    const uint8_t* seeds[],
    const size_t seedLens[],
    size_t numSeeds,
    const uint8_t programId[32],
    uint8_t pdaOut[32],
    uint8_t* bumpOut)
{
    size_t totalSeedLen = 0;
    for (size_t i = 0; i < numSeeds; i++) totalSeedLen += seedLens[i];
    size_t inputLen = totalSeedLen + 1 + 32 + 21;
    std::vector<uint8_t> input(inputLen);

    size_t offset = 0;
    for (size_t i = 0; i < numSeeds; i++) {
        memcpy(input.data() + offset, seeds[i], seedLens[i]);
        offset += seedLens[i];
    }

    for (int bump = 255; bump >= 0; bump--) {
        input[offset] = static_cast<uint8_t>(bump);
        memcpy(input.data() + offset + 1, programId, 32);
        memcpy(input.data() + offset + 33, "ProgramDerivedAddress", 21);
        crypto_hash_sha256(pdaOut, input.data(), inputLen);
        
        // Check if the derived address is off the ed25519 curve
        // crypto_core_ed25519_is_valid_point returns 1 if ON curve, 0 if OFF curve
        if (crypto_core_ed25519_is_valid_point(pdaOut) == 0) {
            // Address is off-curve, this is a valid PDA
            *bumpOut = bump;
            return true;
        }
        // If on-curve, continue to next bump value
    }
    return false;
}

bool SolanaClient::deriveAssociatedTokenAddress(
    const uint8_t owner[32],
    const uint8_t mint[32],
    uint8_t ataOut[32],
    uint8_t* bumpOut)
{
    ESP_LOGI(TAG, "📍 Deriving ATA...");
    const uint8_t* seeds[] = {owner, SPL_TOKEN_PROGRAM_ID, mint};
    const size_t seedLens[] = {32, 32, 32};
    bool ok = findProgramAddress(seeds, seedLens, 3, ASSOCIATED_TOKEN_PROGRAM_ID, ataOut, bumpOut);
    if (ok) {
        ESP_LOGI(TAG, "✅ ATA derived, bump=%u", *bumpOut);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, ataOut, 32, ESP_LOG_INFO);
    }
    return ok;
}

// === RPC ===
bool SolanaClient::fetchRecentBlockhash(uint8_t blockhashOut[32]) {
    ESP_LOGI(TAG, "🔗 Fetching recent blockhash...");

    static char buffer[4096];
    static int bufLen = 0;

    const char* rpcReq = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getLatestBlockhash\",\"params\":[{\"commitment\":\"finalized\"}]}";

    esp_http_client_config_t config = {};
    config.url = rpcUrl_.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 15000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    auto handler = [](esp_http_client_event_t *evt) -> esp_err_t {
        switch (evt->event_id) {
            case HTTP_EVENT_ON_DATA:
                if (bufLen + evt->data_len < sizeof(buffer)) {
                    memcpy(buffer + bufLen, evt->data, evt->data_len);
                    bufLen += evt->data_len;
                }
                break;
            case HTTP_EVENT_ON_FINISH:
                buffer[bufLen] = '\0';
                break;
            default: break;
        }
        return ESP_OK;
    };

    config.event_handler = handler;
    bufLen = 0;
    memset(buffer, 0, sizeof(buffer));

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, rpcReq, strlen(rpcReq));
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200 && bufLen > 0) {
        cJSON* root = cJSON_Parse(buffer);
        if (!root) return false;

        cJSON* blockhash = cJSON_GetObjectItemCaseSensitive(
            cJSON_GetObjectItemCaseSensitive(
                cJSON_GetObjectItemCaseSensitive(root, "result"), "value"),
            "blockhash");

        bool ok = (blockhash && cJSON_IsString(blockhash))
            ? CryptoUtils::base58ToBytes(blockhash->valuestring, blockhashOut)
            : false;
        cJSON_Delete(root);
        return ok;
    }
    return false;
}

// === Transaction Building ===
bool SolanaClient::buildTransaction(
    const uint8_t payerPubkey[32],
    const char* paytoBase58,
    const char* feePayerBase58,
    const char* mintBase58,
    uint64_t amount,
    uint8_t decimals,
    const uint8_t blockhash[32],
    std::vector<uint8_t>& txOut)
{
    ESP_LOGI(TAG, "🔨 Building transaction...");

    uint8_t mint[32], payto[32], feePayer[32];
    if (!CryptoUtils::base58ToBytes(mintBase58, mint) ||
        !CryptoUtils::base58ToBytes(paytoBase58, payto) ||
        !CryptoUtils::base58ToBytes(feePayerBase58, feePayer))
        return false;

    // Derive ATAs dynamically
    uint8_t sourceAta[32], destAta[32];
    uint8_t sourceBump, destBump;
    
    if (!deriveAssociatedTokenAddress(payerPubkey, mint, sourceAta, &sourceBump)) {
        ESP_LOGE(TAG, "❌ Failed to derive source ATA");
        return false;
    }
    ESP_LOGI(TAG, "✅ Source ATA derived with bump=%u", sourceBump);
    
    if (!deriveAssociatedTokenAddress(payto, mint, destAta, &destBump)) {
        ESP_LOGE(TAG, "❌ Failed to derive destination ATA");
        return false;
    }
    ESP_LOGI(TAG, "✅ Destination ATA derived with bump=%u", destBump);

    uint8_t accounts[7][32];
    int accountCount = 0;
    memcpy(accounts[accountCount++], feePayer, 32);
    memcpy(accounts[accountCount++], payerPubkey, 32);
    memcpy(accounts[accountCount++], sourceAta, 32);
    memcpy(accounts[accountCount++], destAta, 32);
    memcpy(accounts[accountCount++], mint, 32);
    memcpy(accounts[accountCount++], SPL_TOKEN_PROGRAM_ID, 32);
    memcpy(accounts[accountCount++], COMPUTE_BUDGET_PROGRAM_ID, 32);

    ByteBuffer instr;
    uint8_t countEnc[3];
    size_t countLen = encodeCompactU16(3, countEnc);
    instr.append(countEnc, countLen);

    // ComputeUnitLimit
    {
        uint8_t programIdx = 6;
        instr.append(&programIdx, 1);
        uint8_t accLen = 0;
        instr.append(&accLen, 1);
        uint8_t data[5] = {0x02,0x40,0x9c,0x00,0x00};
        uint8_t lenEnc[3]; size_t lenSz = encodeCompactU16(5, lenEnc);
        instr.append(lenEnc, lenSz);
        instr.append(data, 5);
    }

    // ComputeUnitPrice
    {
        uint8_t programIdx = 6;
        instr.append(&programIdx, 1);
        uint8_t accLen = 0;
        instr.append(&accLen, 1);
        uint8_t data[9] = {0x03,0x01,0,0,0,0,0,0,0};
        uint8_t lenEnc[3]; size_t lenSz = encodeCompactU16(9, lenEnc);
        instr.append(lenEnc, lenSz);
        instr.append(data, 9);
    }

    // TransferChecked
    {
        uint8_t programIdx = 5;
        instr.append(&programIdx, 1);
        uint8_t accCount = 4;
        instr.append(&accCount, 1);
        uint8_t accIdx[] = {2,4,3,1};
        instr.append(accIdx, 4);
        uint8_t transferData[10];
        transferData[0] = 12;
        for (int i = 0; i < 8; i++) transferData[i+1] = (amount >> (i*8)) & 0xff;
        transferData[9] = decimals;
        uint8_t lenEnc[3]; size_t lenSz = encodeCompactU16(10, lenEnc);
        instr.append(lenEnc, lenSz);
        instr.append(transferData, 10);
    }

    ByteBuffer tx;
    uint8_t header[3] = {2,0,3};
    tx.append(header, 3);
    uint8_t accEnc[3];
    size_t accLen = encodeCompactU16(accountCount, accEnc);
    tx.append(accEnc, accLen);
    for (int i = 0; i < accountCount; i++)
        tx.append(accounts[i], 32);
    tx.append(blockhash, 32);
    tx.append(instr.data.data(), instr.data.size());

    txOut = std::move(tx.data);
    ESP_LOGI(TAG, "✅ Transaction built successfully (%zu bytes)", txOut.size());
    return true;
}

bool SolanaClient::buildSignedTransaction(
    const std::vector<uint8_t>& txMessage,
    const uint8_t signature[64],
    std::string& base64Out)
{
    ESP_LOGI(TAG, "🔐 Building signed transaction...");
    std::vector<uint8_t> fullTx;
    fullTx.reserve(1 + 64 + 64 + txMessage.size());

    fullTx.push_back(0x02);
    fullTx.insert(fullTx.end(), 64, 0); // empty sig
    fullTx.insert(fullTx.end(), signature, signature + 64);
    fullTx.insert(fullTx.end(), txMessage.begin(), txMessage.end());

    base64Out = CryptoUtils::base64Encode(fullTx.data(), fullTx.size());
    return !base64Out.empty();
}
