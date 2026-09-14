#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "gps/gps.h"

bool wigle_begin(void);
bool wigle_logging(void);
void wigle_set_logging(bool on);

/** Ensure file exists with Wigle 1.6 headers. */
bool wigle_ensure_file(void);
bool wigle_clear(void);

size_t wigle_file_size(void);
size_t wigle_free_bytes(void);
uint32_t wigle_wifi_rows(void);
uint32_t wigle_ble_rows(void);
/** Data rows in CSV (Wi‑Fi + BLE), excluding Wigle headers. */
uint32_t wigle_total_rows(void);

bool wigle_append_wifi(const char* mac, const char* ssid, const char* auth, int channel,
                       int freq_mhz, int rssi, const GpsFix* fix);
bool wigle_append_ble(const char* mac, const char* name, int rssi, const GpsFix* fix);

/** Path for SoftAP download (LittleFS). */
const char* wigle_path(void);
