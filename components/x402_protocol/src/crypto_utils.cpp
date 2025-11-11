#include "crypto_utils.h"
#include <esp_log.h>
#include <sodium.h>
#include <cstring>
#include <cstdlib>

static const char* TAG = "CryptoUtils";

static const int8_t b58digits_map[] = {
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6,  7, 8,-1,-1,-1,-1,-1,-1,
    -1, 9,10,11,12,13,14,15, 16,-1,17,18,19,20,21,-1,
    22,23,24,25,26,27,28,29, 30,31,32,-1,-1,-1,-1,-1,
    -1,33,34,35,36,37,38,39, 40,41,42,43,-1,44,45,46,
    47,48,49,50,51,52,53,54, 55,56,57,-1,-1,-1,-1,-1,
};

typedef uint64_t b58_maxint_t;
typedef uint32_t b58_almostmaxint_t;
constexpr auto b58_almostmaxint_bits = sizeof(b58_almostmaxint_t) * 8;
constexpr b58_almostmaxint_t b58_almostmaxint_mask = ((((b58_maxint_t)1) << b58_almostmaxint_bits) - 1);

static bool base58_decode(void* bin, size_t* binszp, const char* b58, size_t b58sz) {
    size_t binsz = *binszp;
    const unsigned char* b58u = reinterpret_cast<const unsigned char*>(b58);
    unsigned char* binu = reinterpret_cast<unsigned char*>(bin);
    size_t outisz = (binsz + sizeof(b58_almostmaxint_t) - 1) / sizeof(b58_almostmaxint_t);
    b58_almostmaxint_t outi[outisz];
    b58_maxint_t t;
    b58_almostmaxint_t c;
    size_t i, j;
    uint8_t bytesleft = binsz % sizeof(b58_almostmaxint_t);
    b58_almostmaxint_t zeromask = bytesleft ? (b58_almostmaxint_mask << (bytesleft * 8)) : 0;
    unsigned zerocount = 0;

    if (!b58sz)
        b58sz = strlen(b58);

    for (i = 0; i < outisz; ++i) outi[i] = 0;

    for (i = 0; i < b58sz && b58u[i] == '1'; ++i)
        ++zerocount;

    for (; i < b58sz; ++i) {
        if (b58u[i] & 0x80) return false;
        if (b58digits_map[b58u[i]] == -1) return false;
        c = (unsigned)b58digits_map[b58u[i]];
        for (j = outisz; j--;) {
            t = ((b58_maxint_t)outi[j]) * 58 + c;
            c = t >> b58_almostmaxint_bits;
            outi[j] = t & b58_almostmaxint_mask;
        }
        if (c || (outi[0] & zeromask)) return false;
    }

    j = 0;
    if (bytesleft) {
        for (i = bytesleft; i > 0; --i)
            *(binu++) = (outi[0] >> (8 * (i - 1))) & 0xff;
        ++j;
    }
    for (; j < outisz; ++j) {
        for (i = sizeof(*outi); i > 0; --i)
            *(binu++) = (outi[j] >> (8 * (i - 1))) & 0xff;
    }

    binu = reinterpret_cast<unsigned char*>(bin);
    for (i = 0; i < binsz; ++i)
        if (binu[i]) break;
    *binszp -= i;
    *binszp += zerocount;

    return true;
}

bool CryptoUtils::base58ToBytes(const char* base58_str, uint8_t out32[32]) {
    if (!base58_str) {
        ESP_LOGE(TAG, "Input base58_str is NULL!");
        return false;
    }
    size_t len = strlen(base58_str);
    if (len == 0) {
        ESP_LOGE(TAG, "Input base58_str is empty!");
        return false;
    }

    ESP_LOGI(TAG, "Decoding Base58 string of length %d: '%s'", (int)len, base58_str);
    size_t expected_len = 32;
    bool success = base58_decode(out32, &expected_len, base58_str, len);
    if (!success || expected_len != 32) {
        ESP_LOGE(TAG, "Base58 decode failed");
        return false;
    }
    ESP_LOGI(TAG, "✅ Successfully decoded to 32 bytes");
    return true;
}

char* CryptoUtils::base64Encode(const unsigned char* data, size_t input_length) {
    static const char base64_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t output_length = 4 * ((input_length + 2) / 3);
    char* encoded_data = (char*)calloc(output_length + 1, sizeof(char));
    if (!encoded_data) return NULL;

    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        encoded_data[j++] = base64_table[(triple >> 18) & 0x3F];
        encoded_data[j++] = base64_table[(triple >> 12) & 0x3F];
        encoded_data[j++] = base64_table[(triple >> 6) & 0x3F];
        encoded_data[j++] = base64_table[triple & 0x3F];
    }

    size_t pad = (3 - (input_length % 3)) % 3;
    for (size_t i = 0; i < pad; i++)
        encoded_data[output_length - 1 - i] = '=';

    return encoded_data;
}

bool CryptoUtils::ed25519Sign(
    uint8_t signature[64],
    const uint8_t* message,
    size_t message_len,
    const uint8_t secret_key[32],
    const uint8_t public_key[32]
) {
    ESP_LOGI(TAG, "🔐 Signing message (%d bytes) with ed25519...", (int)message_len);

    uint8_t sk64[64];
    memcpy(sk64, secret_key, 32);
    memcpy(sk64 + 32, public_key, 32);

    unsigned long long sig_len = 0;
    int result = crypto_sign_detached(signature, &sig_len, message, message_len, sk64);
    if (result != 0 || sig_len != 64) {
        ESP_LOGE(TAG, "❌ ED25519 signing failed!");
        return false;
    }

    ESP_LOGI(TAG, "✅ Generated REAL ed25519 signature");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, signature, 64, ESP_LOG_INFO);
    return true;
}
