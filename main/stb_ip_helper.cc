#include "stb_ip_helper.h"
#include <esp_netif.h>
#include <esp_log.h>
#include <cstdio>

#define TAG "STB-IP"

std::string GetStbIp() {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) {
        ESP_LOGW(TAG, "Netif STA gak ada, fallback ke 192.168.1.199");
        return "192.168.1.199";
    }

    esp_netif_ip_info_t ip_info = {};
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        ESP_LOGW(TAG, "Gak bisa baca IP, fallback");
        return "192.168.1.199";
    }

    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));

    // 3rd octet (little-endian): shift 16
    uint8_t oct3 = (ip_info.ip.addr >> 16) & 0xFF;

    ESP_LOGI(TAG, "Xiaozhi IP: %s (subnet 192.168.%d.x)", ip_str, oct3);

    if (oct3 == 1) {
        return "192.168.1.199";   // Kerja
    }
    if (oct3 == 2) {
        return "192.168.2.200";   // Rumah
    }

    ESP_LOGW(TAG, "Subnet gak dikenal, fallback");
    return "192.168.1.199";
}
