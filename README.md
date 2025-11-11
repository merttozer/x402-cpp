# ESP32 X402 Payment Client

A comprehensive C/C++ library implementation of the [X402 Payment Protocol](https://github.com/coinbase/x402) for IoT/Embedded Devices, specifically optimized for the ESP32-C6. This library enables resource-constrained embedded devices to participate in blockchain-based payment flows using the X402 standard.

[![License](https://img.shields.io/badge/License-AGPL--3.0%20%2F%20Commercial-blue.svg)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.0%2B-blue)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![Platform](https://img.shields.io/badge/platform-ESP32--C6-green)](https://www.espressif.com/en/products/socs/esp32-c6)
[![Demo](https://img.shields.io/badge/Demo-YouTube-red)](https://youtube.com/shorts/yQqeUG1CKlk)

## 🎬 Live Demo

Check out the ESP32 X402 Payment Client in action making real payments on Solana Devnet:

**[▶️ Watch Demo on YouTube Shorts](https://youtube.com/shorts/yQqeUG1CKlk)**

This demo uses the [PayAI Echo Merchant](https://x402.payai.network/) service to demonstrate the complete X402 payment flow.

Wallet used: https://explorer.solana.com/address/2KUCmtebQBgQS78QzBJGMWfuq6peTcvjUD7mUnyX2yZ1?cluster=devnet

## 🌟 Features

- **Full X402 Protocol Support**: Complete implementation of the X402 payment protocol specification
- **Solana Integration**: Native support for Solana blockchain transactions on devnet/mainnet
- **SPL Token Transfers**: Built-in support for SPL token transfers with Associated Token Accounts (ATA)
- **Hardware Wallet Functionality**: Secure key management and transaction signing on-device
- **WiFi Connectivity**: Automated WiFi connection management
- **Display Support**: LVGL-based UI for payment status and user interaction
- **Low Resource Footprint**: Optimized for embedded systems with limited RAM/Flash
- **Modular Architecture**: Clean separation of concerns with reusable components
- **Cryptographic Security**: Ed25519 signing using libsodium
- **SPIFFS Configuration**: File-based configuration management

## 🧪 Test Merchant

This project is demonstrated using **[PayAI Echo Merchant](https://x402.payai.network/)**, a test service that implements the X402 protocol for Solana Devnet. 

The Echo Merchant:
- Returns payment offers in response to HTTP 402 requests
- Accepts SPL token payments on Solana Devnet
- Echoes back premium content upon successful payment
- Provides a complete end-to-end X402 payment flow for testing

**Default Configuration**:
```json
{
  "payai_url": "https://x402.payai.network/api/solana-devnet/paid-content",
  "solana_rpc_url": "https://api.devnet.solana.com",
  "token_mint": "4zMMC9srt5Ri5X14GAgXhaHii3GnPAEERYPJgZJDncDU"
}
```

You can use the PayAI Echo Merchant for testing or configure your own X402-compatible merchant endpoint.

## 📋 Table of Contents

- [Architecture](#architecture)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [Installation](#installation)
- [Configuration](#configuration)
- [Usage](#usage)
- [API Reference](#api-reference)
- [Protocol Flow](#protocol-flow)
- [Project Structure](#project-structure)
- [Examples](#examples)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)
- [License](#license)

## 🏗️ Architecture

The library follows a modular architecture with clear separation of concerns:

```
┌─────────────────────────────────────────┐
│           Application Layer             │
│         (app_main.cpp)                  │
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│        X402 Payment Client              │
│      (Orchestrates payment flow)        │
└─┬────────┬────────┬────────┬────────────┘
  │        │        │        │
  ▼        ▼        ▼        ▼
┌────┐   ┌────┐   ┌────┐   ┌────┐
│WiFi│   │HTTP│   │Sol │   │Disp│
│Mgr │   │Clnt│   │ana │   │lay │
└────┘   └────┘   └────┘   └────┘
  │        │        │
  ▼        ▼        ▼
┌────────────────────────┐
│   Crypto Utilities     │
│  (Ed25519, Base58/64)  │
└────────────────────────┘
```

### Core Components

1. **X402PaymentClient**: Main orchestrator for the payment protocol
2. **SolanaClient**: Handles Solana RPC communication and transaction building
3. **HttpClient**: Manages HTTP/HTTPS requests with X-PAYMENT header support
4. **WiFiManager**: Automated WiFi connection and reconnection
5. **CryptoUtils**: Cryptographic primitives (Ed25519, Base58, Base64)
6. **ConfigManager**: SPIFFS-based configuration loading
7. **DisplayManager**: LVGL-based UI for status and user feedback

## 🔧 Hardware Requirements

### Tested Hardware

- **MCU**: ESP32-C6
- **Display**: 240x280 LCD with CST816S touch controller
- **Storage**: Minimum 4MB Flash
- **RAM**: Minimum 512KB

## 📦 Software Requirements

### Prerequisites

- **ESP-IDF**: v5.0 or higher ([Installation Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/))
- **CMake**: v3.16 or higher
- **Python**: 3.8 or higher

### Dependencies

The following dependencies are automatically managed by ESP-IDF Component Manager:

| Component | Version | Purpose |
|-----------|---------|---------|
| `libsodium` | ^1.0.20 | Ed25519 cryptography |
| `lvgl` | ^9.4.0 | Display graphics |
| `esp_lvgl_port` | ^2.6.2 | LVGL ESP32 integration |
| `esp_lcd_touch_cst816s` | ^1.0.6 | Touch controller driver |
| `esp_http_client` | (built-in) | HTTP/HTTPS client |
| `mbedtls` | (built-in) | TLS/SSL support |
| `cJSON` | (built-in) | JSON parsing |

## 🚀 Installation

### 1. Clone the Repository

```bash
git clone https://github.com/merttozer/x402-cpp
cd x402-cpp
```

### 2. Set Up ESP-IDF Environment

```bash
# Source ESP-IDF environment
. $HOME/esp/esp-idf/export.sh

# Or if using ESP-IDF v5.0+
get_idf
```

### 3. Install Dependencies

Dependencies are automatically installed via ESP-IDF Component Manager during the build process.

### 4. Configure the Project

```bash
# Set target device (ESP32-C6)
idf.py set-target esp32c6

# Open configuration menu (optional)
idf.py menuconfig
```

### 5. Build the Project

```bash
idf.py build
```

### 6. Flash to Device

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with your actual serial port.

## ⚙️ Configuration

### Configuration File

Create a `config.json` file in the `main/spiffs/` directory:

```json
{
  "wifi_ssid": "YourWiFiSSID",
  "wifi_password": "YourWiFiPassword",
  "payer_public_key": [/* 32 bytes as decimal array */],
  "payer_private_key": [/* 32 bytes as decimal array */],
  "payai_url": "https://x402.payai.network/api/solana-devnet/paid-content",
  "solana_rpc_url": "https://api.devnet.solana.com",
  "user_agent": "x402-esp32c6/1.0",
  "token_mint": "4zMMC9srt5Ri5X14GAgXhaHii3GnPAEERYPJgZJDncDU",
  "token_decimals": 6
}
```

### Configuration Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `wifi_ssid` | string | WiFi network name |
| `wifi_password` | string | WiFi password |
| `payer_public_key` | array[32] | Ed25519 public key (byte array) |
| `payer_private_key` | array[32] | Ed25519 private key (byte array) |
| `payai_url` | string | X402 payment merchant endpoint |
| `solana_rpc_url` | string | Solana RPC endpoint URL |
| `user_agent` | string | HTTP User-Agent header |
| `token_mint` | string | SPL token mint address (Base58) |
| `token_decimals` | integer | Token decimal places (usually 6 or 9) |

### Generating Keypair

To generate a new Solana keypair for testing:

```bash
# Using Solana CLI
solana-keygen new --outfile keypair.json

# Convert to byte array format for config.json
# (You'll need to write a small script to convert the JSON keypair)
```

### Security Considerations

⚠️ **WARNING**: Never commit your private keys to version control!

- Use environment variables or secure key storage for production
- Consider implementing secure element integration for production devices
- The current implementation stores keys in SPIFFS (encrypted flash recommended)

## 💻 Usage

### Basic Example

```cpp
#include "x402_client.h"
#include "config_manager.h"

extern "C" void app_main(void) {
    // Initialize SPIFFS
    if (!ConfigManager::init()) {
        return;
    }

    // Load configuration
    X402Config config = {};
    if (!ConfigManager::load("/spiffs/config.json", config)) {
        return;
    }

    // Create client
    X402PaymentClient client(config);
    
    // Initialize environment (WiFi, crypto, display)
    if (!client.init()) {
        return;
    }

    // Run event loop (blocking)
    client.runEventLoop();
}
```

### Advanced: Custom Payment Flow

```cpp
// Initialize client
X402PaymentClient client(config);
client.init();

// Execute a single payment
bool success = client.executePaymentFlow();

if (success) {
    ESP_LOGI(TAG, "Payment completed successfully!");
} else {
    ESP_LOGE(TAG, "Payment failed!");
}

// Return to idle screen
client.returnToIdleAfterDelay(3000);
```

### Button-Triggered Payments

The library includes a task-based payment system for handling button presses:

```cpp
// This is handled automatically in runEventLoop()
// But you can trigger it manually:
client.onPaymentButtonPressed();
```

## 📚 API Reference

### X402PaymentClient

#### Constructor

```cpp
X402PaymentClient(const X402Config& config)
```

Creates a new payment client with the provided configuration.

#### Methods

##### `bool init()`

Initializes the payment client environment:
- Initializes display
- Initializes libsodium cryptography
- Initializes NVS flash storage
- Connects to WiFi

**Returns**: `true` on success, `false` on failure

##### `bool executePaymentFlow()`

Executes a complete X402 payment flow:
1. Fetches payment offer (HTTP 402 response)
2. Parses payment requirements
3. Fetches Solana blockhash
4. Builds SPL token transfer transaction
5. Signs transaction with Ed25519
6. Submits payment with X-PAYMENT header
7. Returns premium content on success

**Returns**: `true` if payment successful, `false` otherwise

##### `void runEventLoop()`

Starts the main event loop (blocking). Displays idle screen and handles button press events for initiating payments.

##### `void onPaymentButtonPressed()`

Handles payment button press events. Creates a new FreeRTOS task to execute the payment flow asynchronously.

##### `void returnToIdleAfterDelay(uint32_t delay_ms)`

Returns to idle screen after the specified delay.

**Parameters**:
- `delay_ms`: Delay in milliseconds before returning to idle

### SolanaClient

#### Constructor

```cpp
SolanaClient(const std::string& rpcUrl)
```

#### Methods

##### `bool fetchRecentBlockhash(uint8_t blockhashOut[32])`

Fetches the most recent blockhash from Solana RPC endpoint.

**Returns**: `true` on success

##### `bool buildTransaction(...)`

Builds a complete Solana transaction with:
- Compute budget instructions
- SPL token transfer instruction
- Multiple signatures support

##### `bool deriveAssociatedTokenAddress(...)`

Derives the Associated Token Account (ATA) address for a given owner and mint.

### HttpClient

#### Constructor

```cpp
HttpClient(const HttpClientConfig& config)
```

#### Methods

##### `bool get_402(const char* url, cJSON** json_out, char** raw_response = nullptr)`

Performs GET request expecting HTTP 402 Payment Required response.

**Returns**: `true` if 402 response with valid payment offer received

##### `bool submit_payment(const char* url, const char* b64_payment, char** content_out)`

Submits payment by sending X-PAYMENT header with Base64-encoded payment data.

**Returns**: `true` if HTTP 200 received with premium content

### CryptoUtils

Static utility class for cryptographic operations.

#### Methods

##### `static bool base58ToBytes(const char* base58_str, uint8_t out32[32])`

Converts Base58-encoded string to 32-byte array.

##### `static char* base64Encode(const unsigned char* data, size_t input_length)`

Encodes binary data to Base64 string.

##### `static bool ed25519Sign(...)`

Signs message using Ed25519 algorithm.

**Parameters**:
- `signature[64]`: Output signature buffer
- `message`: Message to sign
- `message_len`: Message length
- `secret_key[32]`: Ed25519 private key
- `public_key[32]`: Ed25519 public key

### WiFiManager

#### Constructor

```cpp
WiFiManager(const std::string& ssid, const std::string& password)
```

#### Methods

##### `bool connect()`

Initiates WiFi connection. Blocks until connected or timeout.

**Returns**: `true` if connected successfully

##### `bool isConnected() const`

Checks current WiFi connection status.

**Returns**: `true` if currently connected

## 🔄 Protocol Flow

The X402 payment protocol implementation follows this sequence:

```
┌─────────┐                 ┌─────────┐                 ┌─────────┐
│  Client │                 │ Merchant│                 │ Solana  │
└────┬────┘                 └────┬────┘                 └────┬────┘
     │                           │                           │
     │  1. GET /resource         │                           │
     ├──────────────────────────>│                           │
     │                           │                           │
     │  2. 402 Payment Required  │                           │
     │     (Payment Offer)       │                           │
     │<──────────────────────────┤                           │
     │                           │                           │
     │  3. getLatestBlockhash    │                           │
     ├───────────────────────────┼──────────────────────────>│
     │                           │                           │
     │  4. Blockhash Response    │                           │
     │<──────────────────────────┼───────────────────────────┤
     │                           │                           │
     │  5. Build Transaction     │                           │
     │  6. Sign with Ed25519     │                           │
     │                           │                           │
     │  7. GET /resource         │                           │
     │     X-PAYMENT: <tx>       │                           │
     ├──────────────────────────>│                           │
     │                           │                           │
     │                           │  8. Verify & Send TX      │
     │                           ├──────────────────────────>│
     │                           │                           │
     │                           │  9. TX Confirmation       │
     │                           │<──────────────────────────┤
     │                           │                           │
     │  10. 200 OK               │                           │
     │      (Premium Content)    │                           │
     │<──────────────────────────┤                           │
     │                           │                           │
```

### Step-by-Step Breakdown

1. **Initial Request**: Client requests protected resource
2. **Payment Offer**: Merchant returns 402 with payment requirements in JSON
3. **Blockhash Fetch**: Client fetches recent blockhash from Solana
4. **Transaction Build**: Client constructs SPL token transfer transaction
5. **Signing**: Client signs transaction with Ed25519 private key
6. **Payment Submission**: Client sends signed transaction in X-PAYMENT header
7. **Verification**: Merchant verifies and broadcasts transaction to Solana
8. **Confirmation**: Solana confirms transaction on-chain
9. **Content Delivery**: Merchant returns premium content (HTTP 200)

## 📁 Project Structure

```
esp32-x402-client/
├── components/
│   └── x402_protocol/
│       ├── include/
│       │   ├── config_manager.h
│       │   ├── crypto_utils.h
│       │   ├── display_manager.h
│       │   ├── http_client.h
│       │   ├── solana_client.h
│       │   ├── wifi_manager.h
│       │   └── x402_client.h
│       ├── src/
│       │   ├── config_manager.cpp
│       │   ├── crypto_utils.cpp
│       │   ├── display_manager.cpp
│       │   ├── http_client.cpp
│       │   ├── solana_client.cpp
│       │   ├── wifi_manager.cpp
│       │   └── x402_client.cpp
│       └── CMakeLists.txt
├── main/
│   ├── spiffs/
│   │   └── config.json           # Configuration file
│   ├── app_main.cpp
│   ├── idf_component.yml
│   └── CMakeLists.txt
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig
└── README.md
```

### Component Descriptions

| Component | Responsibility |
|-----------|----------------|
| **config_manager** | SPIFFS initialization and JSON config loading |
| **crypto_utils** | Cryptographic primitives (Ed25519, Base58, Base64) |
| **display_manager** | LVGL-based UI rendering and touch handling |
| **http_client** | HTTP/HTTPS requests with X402 support |
| **solana_client** | Solana RPC, transaction building, ATA derivation |
| **wifi_manager** | WiFi connection and event handling |
| **x402_client** | Main payment protocol orchestration |

## 🎯 Examples

### Example 1: Using PayAI Echo Merchant (Default)

```cpp
// The default configuration uses PayAI Echo Merchant
config.payai_url = "https://x402.payai.network/api/solana-devnet/paid-content";

X402PaymentClient client(config);
client.init();
client.executePaymentFlow();
```

### Example 2: Custom Merchant Integration

```cpp
// Use your own X402-compatible merchant
config.payai_url = "https://your-merchant.com/api/paid-endpoint";

X402PaymentClient client(config);
client.init();
client.executePaymentFlow();
```

### Example 3: Mainnet Configuration

```json
{
  "solana_rpc_url": "https://api.mainnet-beta.solana.com",
  "token_mint": "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v",
  "token_decimals": 6
}
```

### Example 4: Multiple Payments

```cpp
for (int i = 0; i < 5; i++) {
    ESP_LOGI(TAG, "Payment attempt %d", i + 1);
    
    if (client.executePaymentFlow()) {
        ESP_LOGI(TAG, "Payment %d successful", i + 1);
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2 seconds between payments
}
```

## 🔧 Troubleshooting

### Common Issues

#### WiFi Connection Fails

**Symptoms**: `❌ WiFi connection timeout`

**Solutions**:
- Verify SSID and password in config.json
- Check WiFi signal strength
- Ensure 2.4GHz WiFi (ESP32-C6 doesn't support 5GHz)
- Try increasing timeout in `x402_client.cpp`

```cpp
const int max_retries = 20;  // Increase from 10
```

#### libsodium Initialization Failed

**Symptoms**: `❌ libsodium initialization failed`

**Solutions**:
- Ensure libsodium component is properly installed
- Check ESP-IDF version (v5.0+ required)
- Verify dependency in `idf_component.yml`

#### Payment Submission Failed

**Symptoms**: `❌ Payment submission failed`

**Solutions**:
- Verify merchant URL is accessible
- Check RPC endpoint connectivity
- Ensure sufficient token balance in payer account
- Verify private key matches public key
- Check merchant logs for rejection reason

#### Transaction Build Failed

**Symptoms**: `❌ Failed to build transaction`

**Solutions**:
- Verify token mint address is correct
- Ensure Associated Token Accounts exist for both sender/receiver
- Check token decimals match the mint's decimals
- Verify blockhash is recent (< 150 blocks old)

#### Memory Issues

**Symptoms**: Crashes, stack overflows, allocation failures

**Solutions**:
- Increase task stack size in `x402_client.cpp`:
```cpp
xTaskCreate(paymentTaskWrapper, "payment_task", 16384, ...);  // Increase from 8192
```
- Enable PSRAM if available
- Reduce HTTP buffer sizes if needed

### Debug Logging

Enable verbose logging for specific components:

```cpp
esp_log_level_set("x402", ESP_LOG_DEBUG);
esp_log_level_set("SolanaClient", ESP_LOG_DEBUG);
esp_log_level_set("HttpClient", ESP_LOG_DEBUG);
```

### Monitor Serial Output

```bash
idf.py monitor
```

Use filters to focus on specific components:

```bash
idf.py monitor | grep "x402\|SolanaClient"
```

## 🤝 Contributing

Contributions are welcome! Please follow these guidelines:

### Development Setup

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/amazing-feature`
3. Make your changes
4. Run tests (if available)
5. Commit with clear messages: `git commit -m 'Add amazing feature'`
6. Push to your fork: `git push origin feature/amazing-feature`
7. Open a Pull Request

### Code Style

- Follow ESP-IDF coding standards
- Use meaningful variable and function names
- Add comments for complex logic
- Keep functions focused and modular
- Use C++11/14 features appropriately

### Pull Request Checklist

- [ ] Code compiles without warnings
- [ ] All features tested on actual hardware
- [ ] Documentation updated (README, code comments)
- [ ] No private keys or secrets committed
- [ ] CHANGELOG updated (if applicable)

## 🛣️ Roadmap

### Short-term (v1.1)

- [ ] Add support for multiple token types
- [ ] Implement transaction retry logic
- [ ] Add detailed error codes
- [ ] Improve display UI/UX
- [ ] Add OTA update support

### Medium-term (v1.2)

- [ ] Support for Ethereum/EVM chains
- [ ] Hardware secure element integration
- [ ] BLE payment initiation
- [ ] QR code scanning support
- [ ] Multi-signature support

### Long-term (v2.0)

- [ ] Lightning Network integration
- [ ] Cross-chain atomic swaps
- [ ] Payment streaming
- [ ] Recurring payment subscriptions
- [ ] Mobile app companion

## 📄 License

This project is dual-licensed:

### Open Source License (AGPL-3.0)

Free to use under **GNU Affero General Public License v3.0** for open source projects. If you use this software in a network service or distribute it, you must share your source code modifications under AGPL-3.0.

**✅ Use AGPL-3.0 if:**
- Your project is open source
- You will share all source code
- Personal/educational/research use
- Internal company use (no external distribution)

### Commercial License

For commercial products, SaaS, IoT devices, or proprietary use without sharing source code, you must purchase a commercial license.

**💼 Use Commercial License if:**
- Building commercial IoT products
- Want to keep modifications private
- Embedding in hardware devices for sale
- Running as a paid SaaS service
- Any proprietary/closed-source use

**Purchase Commercial License**: Contact licensing@[yourdomain].com

See [LICENSE](LICENSE) file for complete terms.

---

## 🙏 Acknowledgments

- [X402 Protocol Specification](https://github.com/Blockchain-Payments/x402-protocol) - Protocol design
- [PayAI Network](https://x402.payai.network/) - Echo Merchant test service for X402 protocol
- [Espressif Systems](https://www.espressif.com/) - ESP-IDF framework
- [LVGL](https://lvgl.io/) - Graphics library
- [libsodium](https://libsodium.gitbook.io/) - Cryptography library
- [Solana](https://solana.com/) - Blockchain infrastructure

## 📞 Support

### Documentation

- [X402 Protocol Docs](https://github.com/Blockchain-Payments/x402-protocol)
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/)
- [Solana Developer Docs](https://docs.solana.com/)

### Community

- GitHub Issues: [Report bugs or request features](https://github.com/merttozer/x402-cpp/issues)
- Discussions: [Ask questions](https://github.com/merttozer/x402-cpp/discussions)

### Contact

- Email: mertt.ozer@hotmail.com
- Twitter: [@mertt_ozer](https://twitter.com/mertt_ozer)

## 📊 Performance Metrics

### Transaction Times

| Operation | Typical Time | Notes |
|-----------|--------------|-------|
| WiFi Connection | 2-5s | Depends on signal strength |
| Payment Offer Fetch | 200-500ms | Network dependent |
| Blockhash Fetch | 300-800ms | RPC endpoint speed |
| Transaction Build | 50-100ms | CPU intensive |
| Transaction Sign | 10-20ms | Ed25519 is fast |
| Payment Submit | 500-2000ms | Includes TX broadcast |
| **Total Flow** | **2-5 seconds** | End-to-end payment |

### Resource Usage

| Resource | Usage | Notes |
|----------|-------|-------|
| Flash | ~1.5MB | Including dependencies |
| RAM | ~200KB | Peak during transaction |
| SPIFFS | 4KB | For config.json |
| Stack (payment task) | 8KB | Configurable |

---

**Built with ❤️ for the Web3 IoT ecosystem**

**Version**: 1.0.0  
**Last Updated**: November 2025  
**Status**: Production Ready ✅