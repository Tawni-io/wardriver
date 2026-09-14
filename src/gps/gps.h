#pragma once

#include <stdint.h>
#include <stdbool.h>

enum GpsFixState : uint8_t {
  kGpsNoData = 0,   // UART quiet / no NMEA yet
  kGpsSearching,    // sentences seen, no valid fix
  kGpsFix,          // valid lat/lon
};

struct GpsFix {
  GpsFixState state;
  bool have_latlon;
  bool have_utc;
  double lat_deg;   // signed decimal degrees
  double lon_deg;
  uint8_t sats;
  uint32_t sentences;
  uint32_t last_byte_ms;
  uint32_t last_fix_ms;
  char utc_stamp[20];  // "YYYY-MM-DD HH:MM:SS" when have_utc
};

/** Start GPS UART. Does not change WUP (so GPIO-wake can probe standby). */
bool gps_begin(void);
/** Drain UART, parse NMEA. Call often from loop (also while SoftAP). */
void gps_loop(void);
void gps_get(GpsFix* out);

/** Optional: mirror raw NMEA lines to USB Serial (bring-up). */
void gps_set_mirror_raw(bool on);

/** True while GPIO1 is driving L76K WUP high (run). */
bool gps_wakeup_is_run(void);
/** WUP high — leave standby (after wake confirm / SoftAP). */
void gps_wake_run(void);
/**
 * WUP low + pad-hold for ESP deep sleep.
 * Returns true if NMEA stopped (standby looks accepted).
 */
bool gps_enter_standby(void);
