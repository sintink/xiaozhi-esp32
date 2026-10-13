#include "espnow_sender.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <cstring>

#define TAG "ESP-NOW"

// MAC WROOM
static const uint8_t WROOM_MAC[6] = {0xF4, 0x2D, 0xC9, 0x89, 0xAD, 0x9C};

typedef struct __attribute__((packed)) {
    char state[16];
    char text[200];
} EspNowMsg;

static bool s_initialized = false;

void EspNowInit() {
    if (s_initialized) return;

    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init gagal");
        return;
    }

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, WROOM_MAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    peer.ifidx = WIFI_IF_STA;

    if (esp_now_add_peer(&peer) != ESP_OK) {
        ESP_LOGE(TAG, "Gagal tambah peer WROOM");
        return;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "ESP-NOW siap → WROOM %02X:%02X:%02X:%02X:%02X:%02X",
             WROOM_MAC[0], WROOM_MAC[1], WROOM_MAC[2],
             WROOM_MAC[3], WROOM_MAC[4], WROOM_MAC[5]);
}

void EspNowSend(const std::string& state, const std::string& text) {
    if (!s_initialized) {
        EspNowInit();
        if (!s_initialized) return;
    }

    EspNowMsg msg = {};
    strlcpy(msg.state, state.c_str(), sizeof(msg.state));
    strlcpy(msg.text, text.c_str(), sizeof(msg.text));

    esp_err_t err = esp_now_send(WROOM_MAC, (uint8_t*)&msg, sizeof(msg));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "→ %s | %s", state.c_str(), text.c_str());
    } else {
        ESP_LOGW(TAG, "Gagal kirim: %s", esp_err_to_name(err));
    }
}
