#include "scan/survey.h"

#include "gps/gps.h"
#include "log/wigle_log.h"

#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// Wi‑Fi cadence: denser while moving; geo dedupe stops same-spot CSV spam.
constexpr uint32_t kWifiIntervalMs = 3500;
constexpr uint32_t kFirstWifiDelayMs = 1500;
// Mirrors WiGLE Android DatabaseHelper observation rules (approx).
constexpr double kMediumLatLonChange = 0.001;   // ~100 m
constexpr double kSmallLatLonChange = 0.0001;   // ~11 m
constexpr uint32_t kSmallLocDelayMs = 3600000;  // 1 hour if nearly still
constexpr int kLevelChangeDb = 5;               // WiGLE LEVEL_CHANGE
constexpr uint8_t kRecentMax = 24;
constexpr uint8_t kDedupeSlots = 128;

bool g_active = false;
bool g_ble_on = false;
bool g_wifi_scanning = false;
uint32_t g_logging_since_ms = 0;
uint32_t g_last_wifi_ms = 0;
uint32_t g_wifi_seen = 0;
uint32_t g_ble_seen = 0;

uint8_t g_ch24_count[SURVEY_CH24_N] = {};
int8_t g_ch24_rssi[SURVEY_CH24_N] = {};
uint16_t g_ch24_aps = 0;
uint8_t g_ch5_count[SURVEY_CH5_N] = {};
int8_t g_ch5_rssi[SURVEY_CH5_N] = {};
uint16_t g_ch5_aps = 0;
uint16_t g_last_scan_n = 0;
bool g_wifi_have = false;

const uint8_t kCh5List[SURVEY_CH5_N] = {36,  40,  44,  48,  52,  56,  60,  64,  100,
                                       104, 108, 112, 116, 120, 124, 128, 132, 136,
                                       140, 144, 149, 153, 157, 161, 165};

uint16_t g_ble_rf[SURVEY_BLE_N] = {};
uint16_t g_ble_rf_n = 0;
bool g_ble_have = false;

struct RecentHit {
  char label[24];
  uint8_t mac[6];
  bool have_mac;
  bool is_ble;
  int8_t rssi;
};
RecentHit g_recent[kRecentMax] = {};
uint8_t g_recent_n = 0;
uint32_t g_recent_gen = 0;

struct Dedupe {
  uint8_t mac[6];
  int8_t rssi;
  float lat;
  float lon;
  uint32_t ms;
  bool used;
};
Dedupe g_dedupe[kDedupeSlots] = {};

NimBLEScan* g_scan = nullptr;

void push_recent(const char* label, bool is_ble, int rssi, const uint8_t mac[6]) {
  if (!label || !label[0]) label = "(hidden)";

  RecentHit hit = {};
  snprintf(hit.label, sizeof(hit.label), "%s", label);
  hit.is_ble = is_ble;
  hit.rssi = (int8_t)rssi;
  if (mac) {
    memcpy(hit.mac, mac, 6);
    hit.have_mac = true;
  }

  int found = -1;
  if (hit.have_mac) {
    for (int i = 0; i < (int)g_recent_n; i++) {
      if (g_recent[i].have_mac && memcmp(g_recent[i].mac, hit.mac, 6) == 0) {
        found = i;
        break;
      }
    }
  }

  if (found >= 0) {
    for (int i = found; i > 0; i--) g_recent[i] = g_recent[i - 1];
    g_recent[0] = hit;
    if (found != 0) g_recent_gen++;
  } else {
    const int last = (g_recent_n < kRecentMax) ? (int)g_recent_n : (int)kRecentMax - 1;
    for (int i = last; i > 0; i--) g_recent[i] = g_recent[i - 1];
    g_recent[0] = hit;
    if (g_recent_n < kRecentMax) g_recent_n++;
    g_recent_gen++;
  }
}

bool mac_parse(const char* s, uint8_t out[6]) {
  unsigned v[6];
  if (sscanf(s, "%02x:%02x:%02x:%02x:%02x:%02x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) !=
      6) {
    return false;
  }
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)v[i];
  return true;
}

int oldest_dedupe_slot(void) {
  int oldest = 0;
  uint32_t oldest_ms = g_dedupe[0].ms;
  for (int i = 1; i < kDedupeSlots; i++) {
    if ((int32_t)(g_dedupe[i].ms - oldest_ms) < 0) {
      oldest = i;
      oldest_ms = g_dedupe[i].ms;
    }
  }
  return oldest;
}

void remember_hit(int slot, const uint8_t mac[6], int rssi, double lat, double lon,
                  uint32_t now) {
  memcpy(g_dedupe[slot].mac, mac, 6);
  g_dedupe[slot].rssi = (int8_t)rssi;
  g_dedupe[slot].lat = (float)lat;
  g_dedupe[slot].lon = (float)lon;
  g_dedupe[slot].ms = now;
  g_dedupe[slot].used = true;
}

/** WiGLE-style: new BSSID, moved ~100 m, +5 dB peak, or ~11 m after 1 h. Needs GPS. */
bool should_log(const uint8_t mac[6], int rssi, const GpsFix* fix) {
  if (!fix || fix->state != kGpsFix || !fix->have_latlon) return false;

  const double lat = fix->lat_deg;
  const double lon = fix->lon_deg;
  const uint32_t now = millis();
  int free_slot = -1;

  for (int i = 0; i < kDedupeSlots; i++) {
    if (!g_dedupe[i].used) {
      if (free_slot < 0) free_slot = i;
      continue;
    }
    if (memcmp(g_dedupe[i].mac, mac, 6) != 0) continue;

    const double lat_diff = fabs(lat - (double)g_dedupe[i].lat);
    const double lon_diff = fabs(lon - (double)g_dedupe[i].lon);
    const bool medium_change =
        lat_diff > kMediumLatLonChange || lon_diff > kMediumLatLonChange;
    const bool small_change = lat_diff > kSmallLatLonChange || lon_diff > kSmallLatLonChange;
    const bool small_loc_delay = (now - g_dedupe[i].ms) > kSmallLocDelayMs;
    const bool level_change = rssi >= (int)g_dedupe[i].rssi + kLevelChangeDb;

    if (!(medium_change || (small_loc_delay && small_change) || level_change)) {
      return false;
    }
    remember_hit(i, mac, rssi, lat, lon, now);
    return true;
  }

  const int slot = free_slot >= 0 ? free_slot : oldest_dedupe_slot();
  remember_hit(slot, mac, rssi, lat, lon, now);
  return true;
}

const char* enc_label(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN:
      return "[ESS]";
    case WIFI_AUTH_WEP:
      return "[WEP][ESS]";
    case WIFI_AUTH_WPA_PSK:
      return "[WPA-PSK-CCMP][ESS]";
    case WIFI_AUTH_WPA2_PSK:
      return "[WPA2-PSK-CCMP][ESS]";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "[WPA-PSK-CCMP][WPA2-PSK-CCMP][ESS]";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "[WPA2-EAP-CCMP][ESS]";
    case WIFI_AUTH_WPA3_PSK:
      return "[WPA3-SAE-CCMP][ESS]";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "[WPA2-PSK-CCMP][WPA3-SAE-CCMP][ESS]";
    default:
      return "[ESS]";
  }
}

int channel_freq(int ch) {
  if (ch <= 0) return 0;
  if (ch <= 14) return 2407 + 5 * ch;
  return 5000 + 5 * ch;
}

void reset_wifi_bins(void) {
  memset(g_ch24_count, 0, sizeof(g_ch24_count));
  memset(g_ch5_count, 0, sizeof(g_ch5_count));
  for (int i = 0; i < SURVEY_CH24_N; i++) {
    g_ch24_rssi[i] = -127;
  }
  for (int i = 0; i < SURVEY_CH5_N; i++) {
    g_ch5_rssi[i] = -127;
  }
  g_ch24_aps = 0;
  g_ch5_aps = 0;
  g_last_scan_n = 0;
  g_wifi_have = false;
}

void reset_ble_bins(void) {
  memset(g_ble_rf, 0, sizeof(g_ble_rf));
  g_ble_rf_n = 0;
  g_ble_have = false;
}

int ch5_index(int ch) {
  for (int i = 0; i < SURVEY_CH5_N; i++) {
    if (kCh5List[i] == (uint8_t)ch) return i;
  }
  return -1;
}

void bump_rssi_count(uint8_t* count, int8_t* peak, int rssi) {
  if (*count < 255) {
    (*count)++;
  }
  if ((int8_t)rssi > *peak) {
    *peak = (int8_t)rssi;
  }
}

void bin_wifi(int ch, int rssi) {
  if (ch >= 1 && ch <= SURVEY_CH24_N) {
    const uint8_t idx = (uint8_t)(ch - 1);
    bump_rssi_count(&g_ch24_count[idx], &g_ch24_rssi[idx], rssi);
    if (g_ch24_aps < 0xFFFFu) g_ch24_aps++;
    return;
  }
  const int idx = ch5_index(ch);
  if (idx < 0) return;
  bump_rssi_count(&g_ch5_count[idx], &g_ch5_rssi[idx], rssi);
  if (g_ch5_aps < 0xFFFFu) g_ch5_aps++;
}

int ble_rssi_bucket(int rssi) {
  if (rssi >= -40) return 0;
  if (rssi >= -50) return 1;
  if (rssi >= -60) return 2;
  if (rssi >= -70) return 3;
  if (rssi >= -80) return 4;
  if (rssi >= -90) return 5;
  return 6;
}

void bin_ble_rssi(int rssi) {
  const int idx = ble_rssi_bucket(rssi);
  if (g_ble_rf[idx] < 0xFFFFu) g_ble_rf[idx]++;
  if (g_ble_rf_n < 0xFFFFu) g_ble_rf_n++;
  g_ble_have = true;
}

void format_mac6(const uint8_t* m, char* out, size_t n) {
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

class BleCallbacks : public NimBLEScanCallbacks {
  void onDiscovered(const NimBLEAdvertisedDevice* adv) override {
    if (!g_active || !wigle_logging() || !adv) return;
    bin_ble_rssi(adv->getRSSI());
  }

  void onResult(const NimBLEAdvertisedDevice* adv) override {
    if (!g_active || !wigle_logging()) return;
    const int rssi = adv->getRSSI();

    std::string addr = adv->getAddress().toString();
    uint8_t mac[6];
    if (!mac_parse(addr.c_str(), mac)) return;

    char macstr[18];
    format_mac6(mac, macstr, sizeof(macstr));
    const char* name = adv->haveName() ? adv->getName().c_str() : "";
    push_recent(name[0] ? name : macstr, true, rssi, mac);

    GpsFix fix = {};
    gps_get(&fix);
    if (!should_log(mac, rssi, &fix)) return;

    if (wigle_append_ble(macstr, name, rssi, &fix)) {
      g_ble_seen++;
    }
  }
} g_ble_cb;

void stop_ble(void) {
  if (!g_ble_on) return;
  if (g_scan) {
    g_scan->stop();
  }
  NimBLEDevice::deinit(true);
  g_scan = nullptr;
  g_ble_on = false;
  Serial.println("Survey BLE stop");
}

bool start_ble(void) {
  if (g_ble_on) return true;
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);
  g_scan = NimBLEDevice::getScan();
  g_scan->setScanCallbacks(&g_ble_cb, true);
  g_scan->setActiveScan(true);
  g_scan->setInterval(160);
  g_scan->setWindow(80);
  g_scan->setMaxResults(0);
  if (!g_scan->start(0, false, true)) {
    Serial.println("Survey BLE start failed");
    NimBLEDevice::deinit(true);
    g_scan = nullptr;
    return false;
  }
  g_ble_on = true;
  Serial.println("Survey BLE start");
  return true;
}

void abort_wifi_scan(void) {
  if (!g_wifi_scanning) return;
  WiFi.scanDelete();
  g_wifi_scanning = false;
}

void finish_wifi_scan(int n) {
  reset_wifi_bins();
  g_last_scan_n = (n > 0) ? (uint16_t)n : 0;
  g_wifi_have = true;
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      bin_wifi(WiFi.channel(i), WiFi.RSSI(i));
    }
  }

  GpsFix fix = {};
  gps_get(&fix);

  if (n > 0 && wigle_logging()) {
    for (int i = 0; i < n; i++) {
      uint8_t mac[6];
      memcpy(mac, WiFi.BSSID(i), 6);
      const int rssi = WiFi.RSSI(i);

      char macstr[18];
      format_mac6(mac, macstr, sizeof(macstr));
      String ssid = WiFi.SSID(i);
      push_recent(ssid.length() ? ssid.c_str() : macstr, false, rssi, mac);

      if (!should_log(mac, rssi, &fix)) continue;

      const int ch = WiFi.channel(i);
      const int freq = channel_freq(ch);
      const char* auth = enc_label(WiFi.encryptionType(i));

      if (wigle_append_wifi(macstr, ssid.c_str(), auth, ch, freq, rssi, &fix)) {
        g_wifi_seen++;
      }
    }
  }
  WiFi.scanDelete();
  g_wifi_scanning = false;
  Serial.printf("WiFi scan %d aps (session wifi=%u ble=%u)\n", n, (unsigned)g_wifi_seen,
                (unsigned)g_ble_seen);

  if (g_active && g_ble_on && g_scan && wigle_logging()) {
    g_scan->start(0, false, true);
  }
}

bool start_wifi_scan(void) {
  if (g_wifi_scanning) return false;
  if (g_ble_on && g_scan) {
    g_scan->stop();
  }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  // Async — must not block the button loop (blocking scans froze GPIO0).
  const int rc = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true);
  if (rc != WIFI_SCAN_RUNNING && rc < 0) {
    Serial.printf("WiFi scan start err %d\n", rc);
    if (g_ble_on && g_scan && wigle_logging()) {
      g_scan->start(0, false, true);
    }
    return false;
  }
  g_wifi_scanning = true;
  return true;
}

}  // namespace

bool survey_begin(void) {
  g_active = false;
  g_wifi_scanning = false;
  g_last_wifi_ms = 0;
  g_logging_since_ms = 0;
  reset_wifi_bins();
  reset_ble_bins();
  return true;
}

void survey_apply_logging(bool on) {
  if (on) {
    g_wifi_seen = 0;
    g_ble_seen = 0;
    g_logging_since_ms = millis();
    g_last_wifi_ms = 0;
    reset_wifi_bins();
    reset_ble_bins();
    if (g_active) {
      start_ble();
    }
  } else {
    abort_wifi_scan();
    stop_ble();
    g_logging_since_ms = 0;
    reset_wifi_bins();
    reset_ble_bins();
  }
}

void survey_pause(void) {
  g_active = false;
  abort_wifi_scan();
  stop_ble();
  WiFi.mode(WIFI_OFF);
  delay(50);
  Serial.println("Survey paused");
}

void survey_resume(void) {
  g_active = true;
  g_last_wifi_ms = 0;
  // Never auto-start radios here — logging is explicit (cabin / SoftAP).
  Serial.println("Survey resumed (idle until logging starts)");
}

void survey_loop(void) {
  if (!g_active) return;

  if (!wigle_logging()) {
    if (g_wifi_scanning) {
      abort_wifi_scan();
    }
    if (g_ble_on) {
      stop_ble();
    }
    return;
  }

  if (!g_ble_on && !g_wifi_scanning) {
    start_ble();
  }

  if (g_wifi_scanning) {
    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
      return;
    }
    if (n >= 0) {
      finish_wifi_scan(n);
    } else {
      Serial.printf("WiFi scan failed (%d)\n", n);
      abort_wifi_scan();
      if (g_ble_on && g_scan) {
        g_scan->start(0, false, true);
      }
    }
    return;
  }

  const uint32_t now = millis();
  if (g_logging_since_ms != 0 && (now - g_logging_since_ms) < kFirstWifiDelayMs) {
    return;
  }
  if (g_last_wifi_ms != 0 && (now - g_last_wifi_ms) < kWifiIntervalMs) {
    return;
  }
  if (start_wifi_scan()) {
    g_last_wifi_ms = now;
  }
}

uint32_t survey_wifi_seen(void) { return g_wifi_seen; }
uint32_t survey_ble_seen(void) { return g_ble_seen; }
uint32_t survey_session_rows(void) { return g_wifi_seen + g_ble_seen; }
bool survey_active(void) { return g_active; }

bool survey_recent_get(uint8_t index, char* out, size_t out_len, bool* is_ble, int8_t* rssi) {
  if (!out || out_len == 0 || index >= g_recent_n) return false;
  snprintf(out, out_len, "%s", g_recent[index].label);
  if (is_ble) *is_ble = g_recent[index].is_ble;
  if (rssi) *rssi = g_recent[index].rssi;
  return true;
}

uint8_t survey_recent_count(void) { return g_recent_n; }

uint32_t survey_recent_gen(void) { return g_recent_gen; }

void survey_ch24_get(uint8_t count[SURVEY_CH24_N], int8_t peak_rssi[SURVEY_CH24_N],
                     uint16_t* aps_24, uint16_t* aps_scan) {
  if (count) {
    memcpy(count, g_ch24_count, sizeof(g_ch24_count));
  }
  if (peak_rssi) {
    memcpy(peak_rssi, g_ch24_rssi, sizeof(g_ch24_rssi));
  }
  if (aps_24) *aps_24 = g_ch24_aps;
  if (aps_scan) *aps_scan = g_last_scan_n;
}

void survey_ch5_get(uint8_t count[SURVEY_CH5_N], int8_t peak_rssi[SURVEY_CH5_N],
                    uint16_t* aps_5, uint16_t* aps_scan) {
  if (count) {
    memcpy(count, g_ch5_count, sizeof(g_ch5_count));
  }
  if (peak_rssi) {
    memcpy(peak_rssi, g_ch5_rssi, sizeof(g_ch5_rssi));
  }
  if (aps_5) *aps_5 = g_ch5_aps;
  if (aps_scan) *aps_scan = g_last_scan_n;
}

uint8_t survey_ch5_channel(uint8_t index) {
  if (index >= SURVEY_CH5_N) return 0;
  return kCh5List[index];
}

bool survey_wifi_have_scan(void) { return g_wifi_have; }

void survey_ble_rf_get(uint16_t count[SURVEY_BLE_N], uint16_t* ads) {
  if (count) {
    memcpy(count, g_ble_rf, sizeof(g_ble_rf));
  }
  if (ads) *ads = g_ble_rf_n;
}

bool survey_ble_rf_have(void) { return g_ble_have; }
