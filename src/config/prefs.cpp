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
/** Dropped with SoftAP map / STA join — clear leftover NVS. */
constexpr const char* kKeyWifiSsidLegacy = "wssid";
constexpr const char* kKeyWifiPassLegacy = "wpass";

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
  if (g_prefs.isKey(kKeyWifiSsidLegacy)) {
    g_prefs.remove(kKeyWifiSsidLegacy);
  }
  if (g_prefs.isKey(kKeyWifiPassLegacy)) {
    g_prefs.remove(kKeyWifiPassLegacy);
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

void prefs_factory_reset(void) {
  prefs_reset_display();
  g_prefs.clear();
}
