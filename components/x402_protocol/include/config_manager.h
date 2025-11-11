#pragma once
#include "x402_client.h"

class ConfigManager {
public:
    static bool init();  // Mounts SPIFFS (new)
    static bool load(const char* path, X402Config& out_config);
};
