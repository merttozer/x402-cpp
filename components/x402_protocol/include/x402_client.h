#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include "solana_client.h"
#include "wifi_manager.h"
#include "http_client.h"
#include "display_manager.h"

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

    /**
     * @brief Initialize display and environment (crypto, NVS, WiFi)
     * @return true if initialization successful
     */
    bool init();
    
    /**
     * @brief Start the event loop (blocking)
     * Handles idle state and payment processing
     */
    void runEventLoop();
    
    /**
     * @brief Execute the complete payment flow
     * @return true if payment successful
     */
    bool executePaymentFlow();
    
    /**
     * @brief Return to idle screen after a delay
     * @param delay_ms Delay in milliseconds
     */
    void returnToIdleAfterDelay(uint32_t delay_ms);

private:
    bool fetchPaymentOffer(cJSON** offer_json);
    
    void onPaymentButtonPressed();  // Callback for button press

    static char* buildPaymentPayload(const char* base64_tx);

    X402Config cfg_;
    std::unique_ptr<SolanaClient> solana_;
    std::unique_ptr<WiFiManager> wifi_;
    std::unique_ptr<HttpClient> http_;
    std::unique_ptr<DisplayManager> display_;
    
    bool env_initialized_;
};