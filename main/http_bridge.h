#pragma once
#include "http_bridge.h"
#include <esp_http_client.h>
#include <esp_log.h>
#include <string>

void HttpBridgeSend(const std::string& state, const std::string& text);
void HttpBridgeInit();
