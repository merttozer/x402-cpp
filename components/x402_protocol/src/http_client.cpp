#include "http_client.h"
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <string.h>


char HttpClient::response_buffer[4096];
int HttpClient::response_len = 0;

esp_err_t HttpClient::event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            response_len = 0;
            break;
        case HTTP_EVENT_ON_DATA:
            if (response_len + evt->data_len < sizeof(response_buffer)) {
                memcpy(response_buffer + response_len, evt->data, evt->data_len);
                response_len += evt->data_len;
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            response_buffer[response_len] = '\0';
            break;
        default:
            break;
    }
    return ESP_OK;
}

HttpClient::HttpClient(const HttpClientConfig& config) : cfg_(config) {}

HttpClient::~HttpClient() {}

bool HttpClient::get(const char* url, char** response_out, size_t* response_len_out) {
    response_len = 0;
    memset(response_buffer, 0, sizeof(response_buffer));
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.user_agent = cfg_.user_agent;
    config.timeout_ms = cfg_.timeout_ms;
    config.event_handler = event_handler;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err == ESP_OK && status == 200) {
        if (response_out) {
            *response_out = (char*)malloc(response_len + 1);
            memcpy(*response_out, response_buffer, response_len);
            (*response_out)[response_len] = '\0';
        }
        if (response_len_out) *response_len_out = response_len;
        return true;
    }
    return false;
}

bool HttpClient::get_402(const char* url, cJSON** json_out, char** raw_response) {
    response_len = 0;
    memset(response_buffer, 0, sizeof(response_buffer));
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.user_agent = cfg_.user_agent;
    config.timeout_ms = cfg_.timeout_ms;
    config.event_handler = event_handler;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err == ESP_OK && status == 402 && response_len > 0) {
        cJSON* root = cJSON_Parse(response_buffer);
        if (root && cJSON_GetObjectItem(root, "accepts")) {
            *json_out = root;
            if (raw_response) {
                *raw_response = (char*)malloc(response_len + 1);
                memcpy(*raw_response, response_buffer, response_len);
                (*raw_response)[response_len] = '\0';
            }
            return true;
        }
        if (root) cJSON_Delete(root);
    }
    return false;
}

bool HttpClient::submit_payment(const char* url, const char* b64_payment, char** content_out) {
    response_len = 0;
    memset(response_buffer, 0, sizeof(response_buffer));
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.user_agent = cfg_.user_agent;
    config.timeout_ms = 20000;
    config.event_handler = event_handler;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    // 🚀 Increase TX and RX buffers for large headers + responses
    config.buffer_size_tx = 2048;  // for large X-PAYMENT header
    config.buffer_size = 4096;     // for response body

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_http_client_set_header(client, "X-PAYMENT", b64_payment);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (err == ESP_OK && status == 200 && response_len > 0) {
        if (content_out) {
            *content_out = (char*)malloc(response_len + 1);
            memcpy(*content_out, response_buffer, response_len);
            (*content_out)[response_len] = '\0';
        }
        esp_http_client_cleanup(client);
        return true;
    }
    esp_http_client_cleanup(client);
    return false;
}