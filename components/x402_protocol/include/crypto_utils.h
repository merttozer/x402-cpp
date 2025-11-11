#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

/**
 * @brief Unified crypto utility class for Solana and PayAI ESP32 clients.
 */
class CryptoUtils {
public:
    /**
     * @brief Decode a Base58-encoded Solana public key into 32 bytes.
     */
    static bool base58ToBytes(const char* base58_str, uint8_t out32[32]);

    /**
     * @brief Sign a message with ED25519 using secret + public key (32 bytes each).
     */
    static bool ed25519Sign(
        uint8_t signature[64],
        const uint8_t* message,
        size_t message_len,
        const uint8_t secret_key[32],
        const uint8_t public_key[32]
    );

    /**
     * @brief Encode binary data into Base64 (null-terminated string).
     * Caller must free() the returned pointer.
     */
    static char* base64Encode(const unsigned char* data, size_t input_length);
};
