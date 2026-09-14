#pragma once

#include <stdbool.h>

constexpr const char* kSoftApSsid = "TawniWardriver";

bool softap_start(void);
void softap_stop(void);
void softap_loop(void);
bool softap_active(void);

/** SoftAP IPv4 string (e.g. "192.168.4.1"), or empty if inactive. */
const char* softap_ip(void);

/** Set by /stop — main loop should call softap_stop(). */
bool softap_stop_requested(void);
void softap_clear_stop_request(void);

/** True while SoftAP firmware upload is in progress (block soft power-off). */
bool softap_ota_active(void);
