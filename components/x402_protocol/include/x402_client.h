#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include "solana_client.h"
#include "wifi_manager.h"
#include "http_client.h"

struct X402Config {
    const char* wifi_ssid;
    const char* wifi_password;
    uint8_t payer_private_key[32];
    uint8_t payer_public_key[32];
    const char* token_mint;
    uint8_t token_decimals;
    const char* payai_url;
    const char* solana_rpc_url;
    const char* user_agent;
};

class X402PaymentClient {
public:
    explicit X402PaymentClient(const X402Config& config);

    bool init();  // Initialize libsodium, NVS, WiFi
    bool run();   // Executes the full x402 payment flow

private:
    bool fetchPaymentOffer(cJSON** offer_json);
    bool processPayment(cJSON* offer_json);

    static char* buildPaymentPayload(const char* base64_tx);

    X402Config cfg_;
    std::unique_ptr<SolanaClient> solana_;
    std::unique_ptr<WiFiManager> wifi_;
    std::unique_ptr<HttpClient> http_;
};
