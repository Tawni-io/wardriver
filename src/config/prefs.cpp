#include "config/prefs.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace {

Preferences g_prefs;
constexpr const char* kNs = "wardriver";
/** New key — do not reuse legacy flip-180 "orient". */
constexpr const char* kKeyLayout = "layout";
constexpr const char* kKeyOrientLegacy = "orient";
constexpr const char* kKeyLoggingLegacy = "logging";
constexpr const char* kKeyWifiSsid = "wssid";
constexpr const char* kKeyWifiPass = "wpass";

}  // namespace

bool prefs_begin(void) {
  if (!g_prefs.begin(kNs, false)) return false;
  if (g_prefs.isKey(kKeyLoggingLegacy)) {
    g_prefs.remove(kKeyLoggingLegacy);
  }
  // Drop old Flip-180 preference so it cannot map to Landscape by accident.
  if (g_prefs.isKey(kKeyOrientLegacy)) {
    g_prefs.remove(kKeyOrientLegacy);
  }
  return true;
}

uint8_t prefs_get_orient(void) {
  if (!g_prefs.isKey(kKeyLayout)) {
    return kOrientPortrait;
  }
  const uint8_t v = g_prefs.getUChar(kKeyLayout, kOrientPortrait);
  return (v == kOrientLandscape) ? kOrientLandscape : kOrientPortrait;
}

bool prefs_set_orient(uint8_t orient) {
  const uint8_t v = (orient == kOrientLandscape) ? kOrientLandscape : kOrientPortrait;
  g_prefs.putUChar(kKeyLayout, v);
  return prefs_get_orient() == v;
}

void prefs_reset_display(void) {
  if (g_prefs.isKey(kKeyLayout)) {
    g_prefs.remove(kKeyLayout);
  }
  if (g_prefs.isKey(kKeyOrientLegacy)) {
    g_prefs.remove(kKeyOrientLegacy);
  }
}

bool prefs_wifi_configured(void) {
  if (!g_prefs.isKey(kKeyWifiSsid)) return false;
  const String s = g_prefs.getString(kKeyWifiSsid, "");
  return s.length() > 0;
}

void prefs_get_wifi(char* ssid, size_t ssid_len, char* pass, size_t pass_len) {
  if (ssid && ssid_len) ssid[0] = '\0';
  if (pass && pass_len) pass[0] = '\0';
  if (ssid && ssid_len) {
    const String s = g_prefs.getString(kKeyWifiSsid, "");
    snprintf(ssid, ssid_len, "%s", s.c_str());
  }
  if (pass && pass_len) {
    const String p = g_prefs.getString(kKeyWifiPass, "");
    snprintf(pass, pass_len, "%s", p.c_str());
  }
}

bool prefs_set_wifi(const char* ssid, const char* pass) {
  if (!ssid || !ssid[0]) {
    prefs_clear_wifi();
    return false;
  }
  g_prefs.putString(kKeyWifiSsid, ssid);
  g_prefs.putString(kKeyWifiPass, pass ? pass : "");
  return prefs_wifi_configured();
}

void prefs_clear_wifi(void) {
  if (g_prefs.isKey(kKeyWifiSsid)) g_prefs.remove(kKeyWifiSsid);
  if (g_prefs.isKey(kKeyWifiPass)) g_prefs.remove(kKeyWifiPass);
}

void prefs_factory_reset(void) {
  prefs_reset_display();
  prefs_clear_wifi();
  g_prefs.clear();
}
