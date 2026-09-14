#include "gps/gps.h"

#include "board_config.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

namespace {

HardwareSerial g_uart(1);

GpsFix g_fix = {};
bool g_mirror = false;
bool g_uart_up = false;

constexpr uint32_t kStandbyListenMs = 1200;

char g_line[128];
size_t g_line_len = 0;

bool sentence_is(const char* line, const char* type3) {
  // $GPxxx / $GNxxx / $BDxxx — type starts after talker (2 chars).
  if (!line || line[0] != '$' || strlen(line) < 6) return false;
  return line[3] == type3[0] && line[4] == type3[1] && line[5] == type3[2];
}

/** Return pointer to start of field `idx` (0 = after '$talker'). */
const char* field_ptr(const char* line, int idx) {
  if (!line) return nullptr;
  const char* p = line;
  // skip talker type through first comma after $XXXXX
  while (*p && *p != ',') p++;
  if (*p == ',') p++;
  for (int i = 0; i < idx; i++) {
    while (*p && *p != ',' && *p != '*') p++;
    if (*p == ',') p++;
    else return nullptr;
  }
  return p;
}

bool field_empty(const char* p) {
  return !p || *p == ',' || *p == '*' || *p == '\0';
}

bool parse_latlon_dm(const char* dm, char hemi, double* out_deg) {
  if (!dm || !out_deg || field_empty(dm) || (hemi != 'N' && hemi != 'S' && hemi != 'E' && hemi != 'W')) {
    return false;
  }
  // ddmm.mmm or dddmm.mmm
  const char* dot = strchr(dm, '.');
  if (!dot) return false;
  const int lead = (int)(dot - dm);
  if (lead < 3) return false;
  const int deg_digits = lead - 2;
  char degbuf[8];
  if (deg_digits <= 0 || deg_digits >= (int)sizeof(degbuf)) return false;
  memcpy(degbuf, dm, (size_t)deg_digits);
  degbuf[deg_digits] = '\0';
  const double deg = atof(degbuf);
  const double minutes = atof(dm + deg_digits);
  double v = deg + (minutes / 60.0);
  if (hemi == 'S' || hemi == 'W') v = -v;
  if (!isfinite(v)) return false;
  *out_deg = v;
  return true;
}

void handle_rmc(const char* line) {
  const char* tim = field_ptr(line, 0);
  const char* status = field_ptr(line, 1);
  const char* lat = field_ptr(line, 2);
  const char* ns = field_ptr(line, 3);
  const char* lon = field_ptr(line, 4);
  const char* ew = field_ptr(line, 5);
  const char* date = field_ptr(line, 8);
  if (!status) return;

  g_fix.sentences++;

  // UTC stamp from RMC even before position is valid (useful for logs later).
  if (tim && date && !field_empty(tim) && !field_empty(date) && strlen(tim) >= 6 &&
      strlen(date) >= 6) {
    const int hh = (tim[0] - '0') * 10 + (tim[1] - '0');
    const int mm = (tim[2] - '0') * 10 + (tim[3] - '0');
    const int ss = (tim[4] - '0') * 10 + (tim[5] - '0');
    const int dd = (date[0] - '0') * 10 + (date[1] - '0');
    const int mo = (date[2] - '0') * 10 + (date[3] - '0');
    const int yy = (date[4] - '0') * 10 + (date[5] - '0');
    if (hh < 24 && mm < 60 && ss < 60 && dd >= 1 && dd <= 31 && mo >= 1 && mo <= 12) {
      snprintf(g_fix.utc_stamp, sizeof(g_fix.utc_stamp), "20%02d-%02d-%02d %02d:%02d:%02d", yy,
               mo, dd, hh, mm, ss);
      g_fix.have_utc = true;
    }
  }

  if (status[0] != 'A') {
    if (g_fix.state != kGpsFix) {
      g_fix.state = kGpsSearching;
    }
    return;
  }

  double la = 0, lo = 0;
  if (!parse_latlon_dm(lat, ns ? ns[0] : 0, &la)) return;
  if (!parse_latlon_dm(lon, ew ? ew[0] : 0, &lo)) return;

  g_fix.lat_deg = la;
  g_fix.lon_deg = lo;
  g_fix.have_latlon = true;
  g_fix.state = kGpsFix;
  g_fix.last_fix_ms = millis();
}

void handle_gga(const char* line) {
  const char* lat = field_ptr(line, 1);
  const char* ns = field_ptr(line, 2);
  const char* lon = field_ptr(line, 3);
  const char* ew = field_ptr(line, 4);
  const char* qual = field_ptr(line, 5);
  const char* sats = field_ptr(line, 6);
  if (!qual) return;

  g_fix.sentences++;
  const int q = atoi(qual);
  if (sats && !field_empty(sats)) {
    g_fix.sats = (uint8_t)atoi(sats);
  }

  if (q <= 0) {
    if (g_fix.state != kGpsFix) {
      g_fix.state = kGpsSearching;
    }
    return;
  }

  double la = 0, lo = 0;
  if (!parse_latlon_dm(lat, ns ? ns[0] : 0, &la)) return;
  if (!parse_latlon_dm(lon, ew ? ew[0] : 0, &lo)) return;

  g_fix.lat_deg = la;
  g_fix.lon_deg = lo;
  g_fix.have_latlon = true;
  g_fix.state = kGpsFix;
  g_fix.last_fix_ms = millis();
}

void handle_line(char* line) {
  // strip CR and checksum for field parse (keep * out via field_ptr)
  size_t n = strlen(line);
  while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n')) {
    line[--n] = '\0';
  }
  if (n < 6 || line[0] != '$') return;

  if (g_mirror) {
    Serial.println(line);
  }

  if (sentence_is(line, "RMC")) {
    handle_rmc(line);
  } else if (sentence_is(line, "GGA")) {
    handle_gga(line);
  } else if (g_fix.state == kGpsNoData) {
    g_fix.state = kGpsSearching;
    g_fix.sentences++;
  }
}

void feed_char(char c) {
  g_fix.last_byte_ms = millis();
  if (c == '\n' || c == '\r') {
    if (g_line_len > 0) {
      g_line[g_line_len] = '\0';
      handle_line(g_line);
      g_line_len = 0;
    }
    return;
  }
  if (g_line_len + 1 >= sizeof(g_line)) {
    g_line_len = 0;
    return;
  }
  if (g_line_len == 0 && c != '$') {
    return;
  }
  g_line[g_line_len++] = c;
}

void uart_start(void) {
  if (g_uart_up) return;
  g_uart.setRxBufferSize(1024);
  g_uart.begin(GPS_UART_BAUD, SERIAL_8N1, GPS_UART_RX, GPS_UART_TX);
  g_uart_up = true;
}

void drain_uart(void) {
  while (g_uart.available() > 0) {
    (void)g_uart.read();
  }
  g_line_len = 0;
}

void wakeup_release_hold(void) {
  gpio_hold_dis(static_cast<gpio_num_t>(GPS_WAKEUP_PIN));
}

void wakeup_drive(bool run) {
  wakeup_release_hold();
  pinMode(GPS_WAKEUP_PIN, OUTPUT);
  digitalWrite(GPS_WAKEUP_PIN, run ? HIGH : LOW);
}

/** Print RX lines for `ms`. Counts NMEA in the second half (quiet = standby). */
void listen_standby_reply(uint32_t ms, uint16_t* nmea_n, uint16_t* late_nmea_n) {
  *nmea_n = 0;
  *late_nmea_n = 0;
  const uint32_t start = millis();
  const uint32_t late_from = start + (ms / 2);
  char line[128];
  size_t n = 0;
  while ((millis() - start) < ms) {
    while (g_uart.available() > 0) {
      const char c = static_cast<char>(g_uart.read());
      if (c == '\n' || c == '\r') {
        if (n == 0) continue;
        line[n] = '\0';
        n = 0;
        Serial.printf("GPS RX %s\n", line);
        if (line[0] != '$') continue;
        if (sentence_is(line, "RMC") || sentence_is(line, "GGA") || sentence_is(line, "GSA") ||
            sentence_is(line, "GSV")) {
          (*nmea_n)++;
          if (millis() >= late_from) {
            (*late_nmea_n)++;
          }
        }
      } else if (n + 1 < sizeof(line)) {
        line[n++] = c;
      } else {
        n = 0;
      }
    }
    delay(5);
  }
}

}  // namespace

bool gps_begin(void) {
  memset(&g_fix, 0, sizeof(g_fix));
  g_line_len = 0;
  g_uart.end();
  g_uart_up = false;
  delay(20);
  uart_start();
  Serial.printf("GPS UART1 %u baud RX=%d TX=%d WUP pin=%d level=%s\n",
                (unsigned)GPS_UART_BAUD, GPS_UART_RX, GPS_UART_TX, GPS_WAKEUP_PIN,
                gps_wakeup_is_run() ? "H" : "L");
  return true;
}

bool gps_wakeup_is_run(void) { return digitalRead(GPS_WAKEUP_PIN) == HIGH; }

void gps_wake_run(void) {
  wakeup_drive(true);
  Serial.printf("GPS WUP HIGH (pin %d)\n", GPS_WAKEUP_PIN);
}

bool gps_enter_standby(void) {
  uart_start();
  drain_uart();
  wakeup_drive(false);
  const esp_err_t hold = gpio_hold_en(static_cast<gpio_num_t>(GPS_WAKEUP_PIN));
  Serial.printf("GPS WUP LOW (hold %s pin=%d)\n", esp_err_to_name(hold), GPS_WAKEUP_PIN);

  uint16_t nmea_n = 0, late_nmea_n = 0;
  listen_standby_reply(kStandbyListenMs, &nmea_n, &late_nmea_n);

  const bool quiet = (late_nmea_n == 0);
  if (quiet) {
    Serial.println("GPS standby: NMEA stopped");
  } else {
    Serial.println("GPS standby: NMEA still flowing — check WUP wiring");
  }
  Serial.flush();
  return quiet;
}

void gps_loop(void) {
  while (g_uart.available() > 0) {
    feed_char((char)g_uart.read());
  }
  // Stale fix → searching
  if (g_fix.state == kGpsFix && g_fix.last_fix_ms != 0) {
    if ((millis() - g_fix.last_fix_ms) > 10000u) {
      g_fix.state = kGpsSearching;
    }
  }
  // UART quiet (WUP standby, unplugged TX, etc.) — even after sentences were seen.
  if (g_fix.last_byte_ms == 0 || (millis() - g_fix.last_byte_ms) > 2000u) {
    g_fix.state = kGpsNoData;
  }
}

void gps_get(GpsFix* out) {
  if (!out) return;
  *out = g_fix;
  if (g_fix.last_byte_ms == 0 || (millis() - g_fix.last_byte_ms) > 2000u) {
    out->state = kGpsNoData;
  }
}

void gps_set_mirror_raw(bool on) { g_mirror = on; }
