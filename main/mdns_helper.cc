#include "mdns_helper.h"
#include <mdns.h>
#include <esp_log.h>
#include <lwip/netdb.h>
#include <arpa/inet.h>
#include <cstring>

#define TAG "MDNS"

void MdnsInit() {
    static bool initialized = false;
    if (initialized) return;
    
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init gagal: %s", esp_err_to_name(err));
        return;
    }
    
    mdns_hostname_set("xiaozhi");
    mdns_instance_name_set("Xiaozhi Assistant");
    
    initialized = true;
    ESP_LOGI(TAG, "mDNS siap");
}

std::string ResolveHostname(const std::string& hostname) {
    MdnsInit();
    
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    struct addrinfo* res = nullptr;
    int err = getaddrinfo(hostname.c_str(), nullptr, &hints, &res);
    
    if (err != 0 || res == nullptr) {
        ESP_LOGW(TAG, "Gagal resolve %s (err=%d)", hostname.c_str(), err);
        return "";
    }
    
    struct sockaddr_in* addr = (struct sockaddr_in*)res->ai_addr;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
    
    freeaddrinfo(res);
    ESP_LOGI(TAG, "Resolve %s → %s", hostname.c_str(), ip);
    return std::string(ip);
}
