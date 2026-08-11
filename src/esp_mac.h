#pragma once

// esp_read_mac, for the native build.
//
// Beta 18 reads the station MAC directly rather than through the WiFi driver,
// so the address is available even with WiFi off (crosspoint-reader: "Fix MAC
// address reading when WiFi is off"). The simulator has no radio and no efuse,
// so it returns a FIXED, OBVIOUSLY-FAKE address rather than zeroes: a screen
// showing 00-00-00-00-00-00 reads as a bug, while a locally-administered
// 02:… address reads as what it is.

#include <cstdint>

#include "esp_err.h"

typedef enum {
  ESP_MAC_WIFI_STA,
  ESP_MAC_WIFI_SOFTAP,
  ESP_MAC_BT,
  ESP_MAC_ETH,
} esp_mac_type_t;

inline esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type) {
  if (!mac) return ESP_FAIL;
  // 02: locally administered, so it can never collide with a real OUI.
  static const uint8_t kSimMac[6] = {0x02, 0x49, 0x4E, 0x4B, 0x42, 0x00};
  for (int i = 0; i < 6; i++) mac[i] = kSimMac[i];
  // Vary the last byte by interface so two interfaces never look identical.
  mac[5] = static_cast<uint8_t>(type);
  return ESP_OK;
}

// Beta 18 obfuscates stored secrets against the efuse MAC. No efuse here, so
// this returns the same locally-administered address esp_read_mac does — the
// point is a stable per-device key, and stability is what the simulator can
// honestly provide.
inline esp_err_t esp_efuse_mac_get_default(uint8_t *mac) {
  if (!mac) return ESP_FAIL;
  static const uint8_t kSimMac[6] = {0x02, 0x49, 0x4E, 0x4B, 0x42, 0x00};
  for (int i = 0; i < 6; i++) mac[i] = kSimMac[i];
  return ESP_OK;
}
