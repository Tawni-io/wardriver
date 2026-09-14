#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

bool survey_begin(void);
void survey_pause(void);
void survey_resume(void);
/** Start or stop radios for logging (BLE now; Wi‑Fi on next scan tick). */
void survey_apply_logging(bool on);
/** Run Wi‑Fi/BLE scan work when not in SoftAP. */
void survey_loop(void);

uint32_t survey_wifi_seen(void);
uint32_t survey_ble_seen(void);
/** Rows appended this logging session (Wi‑Fi + BLE). */
uint32_t survey_session_rows(void);
bool survey_active(void);

/** Recent hit for cabin (SSID or BLE name). Newest at index 0. */
bool survey_recent_get(uint8_t index, char* out, size_t out_len, bool* is_ble, int8_t* rssi);
uint8_t survey_recent_count(void);
/** Bumps when the recent list changes (for UI auto-scroll). */
uint32_t survey_recent_gen(void);

/** Occupancy from last STA scan. peak_rssi is -127 if empty. */
enum { SURVEY_CH24_N = 13, SURVEY_CH5_N = 25, SURVEY_BLE_N = 7 };
void survey_ch24_get(uint8_t count[SURVEY_CH24_N], int8_t peak_rssi[SURVEY_CH24_N],
                     uint16_t* aps_24, uint16_t* aps_scan);
void survey_ch5_get(uint8_t count[SURVEY_CH5_N], int8_t peak_rssi[SURVEY_CH5_N],
                    uint16_t* aps_5, uint16_t* aps_scan);
uint8_t survey_ch5_channel(uint8_t index);
bool survey_wifi_have_scan(void);
/** BLE ads in the current ~3 s window, binned by RSSI (strong → weak). */
void survey_ble_rf_get(uint16_t count[SURVEY_BLE_N], uint16_t* ads);
bool survey_ble_rf_have(void);
