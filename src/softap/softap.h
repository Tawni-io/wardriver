#pragma once

#include <stdbool.h>

constexpr const char* kSoftApSsid = "TawniWardriver";

/** Broadcast SoftAP setup hotspot. Stops STA portal if active. */
bool softap_start(void);
/**
 * Join saved Wi‑Fi (phone hotspot / LAN) and serve the same portal/map.
 * Survey must stay paused. On failure returns false (caller should SoftAP fallback).
 */
bool softap_sta_start(void);
void softap_stop(void);
void softap_loop(void);
bool softap_active(void);
/** True when portal is SoftAP (not STA). */
bool softap_is_ap(void);
/** True when portal is STA on saved Wi‑Fi. */
bool softap_is_sta(void);

/** Portal IPv4 string, or empty if inactive. */
const char* softap_ip(void);

/** Set by /stop — leave portal to cabin (survey). */
bool softap_stop_requested(void);
void softap_clear_stop_request(void);

/** Set by SoftAP Join Wi‑Fi — leave SoftAP then softap_sta_start(). */
bool softap_join_requested(void);
void softap_clear_join_request(void);

/** Set by portal — drop STA and broadcast TawniWardriver (recovery). */
bool softap_ap_fallback_requested(void);
void softap_clear_ap_fallback_request(void);

/** Optional status banner on next SoftAP home load. */
void softap_set_flash_message(const char* msg, bool ok);

bool softap_ota_active(void);
