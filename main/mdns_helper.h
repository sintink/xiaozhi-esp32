#pragma once
#include <string>

// Resolve hostname (misal "armbian.local") → IP
// Return IP string, atau string kosong kalau gagal
std::string ResolveHostname(const std::string& hostname);

// Init mDNS
void MdnsInit();
