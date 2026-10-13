#include "mdns_helper.h"
#include <mdns.h>
#include <esp_log.h>
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
    std::string name = hostname;
    if (name.size() > 6 && name.substr(name.size() - 6) == ".local") {
        name = name.substr(0, name.size() - 6);
    }
    
    esp_ip4_addr_t addr = {};
    esp_err_t err = mdns_query_a(name.c_str(), 2000, &addr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Gagal resolve %s (err=%d)", name.c_str(), err);
        return "";
    }
    
    char ip[16];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&addr));
    ESP_LOGI(TAG, "Resolve %s → %s", name.c_str(), ip);
    return std::string(ip);
}
