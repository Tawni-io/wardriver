#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/** Cabin layout: 0 = portrait (default), 1 = landscape. SoftAP save + reboot. */
constexpr uint8_t kOrientPortrait = 0;
constexpr uint8_t kOrientLandscape = 1;

constexpr size_t kWifiSsidMax = 32;
constexpr size_t kWifiPassMax = 64;

bool prefs_begin(void);
uint8_t prefs_get_orient(void);
bool prefs_set_orient(uint8_t orient);
/** Reset display prefs (orient → portrait). Factory reset path. */
void prefs_reset_display(void);

/** Saved STA network for map/export (phone hotspot / LAN). Empty SSID = none. */
bool prefs_wifi_configured(void);
void prefs_get_wifi(char* ssid, size_t ssid_len, char* pass, size_t pass_len);
bool prefs_set_wifi(const char* ssid, const char* pass);
void prefs_clear_wifi(void);

/** Wipe all Wardriver NVS keys and reboot-friendly defaults. */
void prefs_factory_reset(void);
