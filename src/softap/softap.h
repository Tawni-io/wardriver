#pragma once

#include <stdbool.h>

constexpr const char* kSoftApSsid = "TawniWardriver";

/** Broadcast SoftAP setup hotspot. */
bool softap_start(void);
void softap_stop(void);
void softap_loop(void);
bool softap_active(void);

/** Portal IPv4 string, or empty if inactive. */
const char* softap_ip(void);

/** Set by /stop — leave portal to cabin (survey). */
bool softap_stop_requested(void);
void softap_clear_stop_request(void);

/** Optional status banner on next SoftAP home load. */
void softap_set_flash_message(const char* msg, bool ok);

bool softap_ota_active(void);
