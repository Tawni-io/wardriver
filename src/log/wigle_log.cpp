#include "log/wigle_log.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <stdio.h>
#include <string.h>

#ifndef TAWNI_VERSION
#define TAWNI_VERSION "0.0.0"
#endif

namespace {

constexpr const char* kPath = "/wigle.csv";
bool g_ready = false;
bool g_logging = false;  // RAM only — always off after reboot
uint32_t g_wifi_rows = 0;
uint32_t g_ble_rows = 0;

void csv_escape(const char* in, char* out, size_t out_len) {
  if (!out || out_len == 0) return;
  out[0] = '\0';
  if (!in) return;
  // Quote if needed
  bool need = false;
  for (const char* p = in; *p; p++) {
    if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
      need = true;
      break;
    }
  }
  if (!need) {
    snprintf(out, out_len, "%s", in);
    return;
  }
  size_t o = 0;
  if (o + 1 < out_len) out[o++] = '"';
  for (const char* p = in; *p && o + 2 < out_len; p++) {
    if (*p == '"') {
      if (o + 2 >= out_len) break;
      out[o++] = '"';
      out[o++] = '"';
    } else {
      out[o++] = *p;
    }
  }
  if (o + 1 < out_len) out[o++] = '"';
  out[o] = '\0';
}

void format_stamp(const GpsFix* fix, char* out, size_t out_len) {
  if (fix && fix->have_utc && fix->utc_stamp[0]) {
    snprintf(out, out_len, "%s", fix->utc_stamp);
  } else {
    out[0] = '\0';  // empty — do not invent wall clock
  }
}

void format_gps_fields(const GpsFix* fix, char* lat, size_t lat_len, char* lon, size_t lon_len) {
  lat[0] = '\0';
  lon[0] = '\0';
  if (fix && fix->state == kGpsFix && fix->have_latlon) {
    snprintf(lat, lat_len, "%.8f", fix->lat_deg);
    snprintf(lon, lon_len, "%.8f", fix->lon_deg);
  }
}

bool write_headers(File& f) {
  char meta[192];
  snprintf(meta, sizeof(meta),
           "WigleWifi-1.6,appRelease=TawniWardriver-%s,model=T-Display-C5,release=%s,"
           "device=TawniWardriver,display=ST7789,board=Lilygo-T-Display-C5,brand=Tawni\n",
           TAWNI_VERSION, TAWNI_VERSION);
  if (f.print(meta) == 0) return false;
  return f.print(
             "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,"
             "CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type\n") > 0;
}

void recount_rows(void) {
  g_wifi_rows = 0;
  g_ble_rows = 0;
  File f = LittleFS.open(kPath, "r");
  if (!f) return;
  // Skip meta + header
  if (f.available()) f.readStringUntil('\n');
  if (f.available()) f.readStringUntil('\n');
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.endsWith("WIFI") || line.indexOf(",WIFI") >= 0) g_wifi_rows++;
    else if (line.endsWith("BLE") || line.indexOf(",BLE") >= 0) g_ble_rows++;
  }
  f.close();
}

}  // namespace

bool wigle_begin(void) {
  if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
    Serial.println("LittleFS mount failed");
    g_ready = false;
    return false;
  }
  g_ready = true;
  if (!LittleFS.exists(kPath)) {
    wigle_ensure_file();
  } else {
    recount_rows();
  }
  Serial.printf("Wigle FS ok size=%u free=%u wifi=%u ble=%u logging=%d\n",
                (unsigned)wigle_file_size(), (unsigned)wigle_free_bytes(),
                (unsigned)g_wifi_rows, (unsigned)g_ble_rows, (int)wigle_logging());
  return true;
}

bool wigle_logging(void) { return g_logging; }

void wigle_set_logging(bool on) { g_logging = on; }

bool wigle_ensure_file(void) {
  if (!g_ready) return false;
  if (LittleFS.exists(kPath)) return true;
  File f = LittleFS.open(kPath, "w");
  if (!f) return false;
  const bool ok = write_headers(f);
  f.close();
  g_wifi_rows = 0;
  g_ble_rows = 0;
  return ok;
}

bool wigle_clear(void) {
  if (!g_ready) return false;
  LittleFS.remove(kPath);
  g_wifi_rows = 0;
  g_ble_rows = 0;
  return wigle_ensure_file();
}

size_t wigle_file_size(void) {
  if (!g_ready || !LittleFS.exists(kPath)) return 0;
  File f = LittleFS.open(kPath, "r");
  if (!f) return 0;
  const size_t n = f.size();
  f.close();
  return n;
}

size_t wigle_free_bytes(void) {
  if (!g_ready) return 0;
  return LittleFS.totalBytes() - LittleFS.usedBytes();
}

uint32_t wigle_wifi_rows(void) { return g_wifi_rows; }
uint32_t wigle_ble_rows(void) { return g_ble_rows; }
uint32_t wigle_total_rows(void) { return g_wifi_rows + g_ble_rows; }

bool wigle_append_wifi(const char* mac, const char* ssid, const char* auth, int channel,
                       int freq_mhz, int rssi, const GpsFix* fix) {
  if (!g_ready || !wigle_logging()) return false;
  // WiGLE trilateration needs positioned observations; skip empty lat/lon.
  if (!fix || fix->state != kGpsFix || !fix->have_latlon) return false;
  if (wigle_free_bytes() < 256) {
    Serial.println("Wigle: flash nearly full");
    return false;
  }
  if (!wigle_ensure_file()) return false;

  char ssid_e[96];
  char auth_e[48];
  char stamp[24];
  char lat[24];
  char lon[24];
  csv_escape(ssid ? ssid : "", ssid_e, sizeof(ssid_e));
  csv_escape(auth ? auth : "", auth_e, sizeof(auth_e));
  format_stamp(fix, stamp, sizeof(stamp));
  format_gps_fields(fix, lat, sizeof(lat), lon, sizeof(lon));

  char line[320];
  snprintf(line, sizeof(line),
           "%s,%s,%s,%s,%d,%d,%d,%s,%s,,,,WIFI\n", mac ? mac : "", ssid_e, auth_e, stamp, channel,
           freq_mhz, rssi, lat, lon);

  File f = LittleFS.open(kPath, "a");
  if (!f) return false;
  const bool ok = f.print(line) > 0;
  f.close();
  if (ok) g_wifi_rows++;
  return ok;
}

bool wigle_append_ble(const char* mac, const char* name, int rssi, const GpsFix* fix) {
  if (!g_ready || !wigle_logging()) return false;
  if (!fix || fix->state != kGpsFix || !fix->have_latlon) return false;
  if (wigle_free_bytes() < 256) return false;
  if (!wigle_ensure_file()) return false;

  char name_e[96];
  char stamp[24];
  char lat[24];
  char lon[24];
  csv_escape(name ? name : "", name_e, sizeof(name_e));
  format_stamp(fix, stamp, sizeof(stamp));
  format_gps_fields(fix, lat, sizeof(lat), lon, sizeof(lon));

  char line[320];
  snprintf(line, sizeof(line), "%s,%s,[LE],%s,0,,%d,%s,%s,,,,BLE\n", mac ? mac : "", name_e,
           stamp, rssi, lat, lon);

  File f = LittleFS.open(kPath, "a");
  if (!f) return false;
  const bool ok = f.print(line) > 0;
  f.close();
  if (ok) g_ble_rows++;
  return ok;
}

const char* wigle_path(void) { return kPath; }
