#pragma once

#include <cJSON.h>
#include <esp_err.h>
#include <esp_http_client.h>  

struct HttpClientConfig {
    const char* user_agent;
    int timeout_ms = 15000;
};

class HttpClient {
public:
    explicit HttpClient(const HttpClientConfig& config);
    ~HttpClient();

    bool get(const char* url, char** response_out, size_t* response_len_out = nullptr);
    bool get_402(const char* url, cJSON** json_out, char** raw_response = nullptr);
    bool submit_payment(const char* url, const char* b64_payment, char** content_out = nullptr);

private:
    static char response_buffer[4096];
    static int response_len;
    HttpClientConfig cfg_;
    static esp_err_t event_handler(esp_http_client_event_t *evt);
};