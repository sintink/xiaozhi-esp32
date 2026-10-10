#include "http_bridge.h"
#include <esp_http_client.h>
#include <esp_log.h>
#include <cstring>

#define WROOM_URL "http://192.168.1.138/api/xiaozhi"

void HttpBridgeInit() {
    // Optional: log kalau perlu
}

static std::string JsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) o += ' ';
        else o += c;
    }
    return o;
}

void HttpBridgeSend(const std::string& state, const std::string& text) {
    std::string body = "{\"state\":\"" + state + 
                       "\",\"text\":\"" + JsonEscape(text) + "\"}";

    esp_http_client_config_t config = {};
    config.url = WROOM_URL;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 3000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body.c_str(), body.size());

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        printf("HTTP-BRIDGE", "%s -> WROOM %d", state.c_str(), status);
    } else {
        printf("HTTP-BRIDGE", "%s -> gagal: %s", state.c_str(), esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}
