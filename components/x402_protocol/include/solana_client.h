#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

class SolanaClient {
public:
    SolanaClient(const std::string& rpcUrl);

    // === PDA & ATA ===
    bool deriveAssociatedTokenAddress(
        const uint8_t owner[32],
        const uint8_t mint[32],
        uint8_t ataOut[32],
        uint8_t* bumpOut
    );

    bool findProgramAddress(
        const uint8_t* seeds[],
        const size_t seedLens[],
        size_t numSeeds,
        const uint8_t programId[32],
        uint8_t pdaOut[32],
        uint8_t* bumpOut
    );

    // === RPC ===
    bool fetchRecentBlockhash(uint8_t blockhashOut[32]);

    // === Transactions ===
    bool buildTransaction(
        const uint8_t payerPubkey[32],
        const char* paytoBase58,
        const char* feePayerBase58,
        const char* mintBase58,
        uint64_t amount,
        uint8_t decimals,
        const uint8_t blockhash[32],
        std::vector<uint8_t>& txOut
    );

    bool buildSignedTransaction(
        const std::vector<uint8_t>& txMessage,
        const uint8_t signature[64],
        std::string& base64Out
    );

private:
    // === Internal helpers ===
    struct ByteBuffer {
        std::vector<uint8_t> data;
        void append(const void* ptr, size_t len);
    };

    static size_t encodeCompactU16(uint16_t value, uint8_t* output);

    static const uint8_t SPL_TOKEN_PROGRAM_ID[32];
    static const uint8_t ASSOCIATED_TOKEN_PROGRAM_ID[32];
    static const uint8_t COMPUTE_BUDGET_PROGRAM_ID[32];

    std::string rpcUrl_;
};
