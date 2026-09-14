#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "gps/gps.h"

bool cabin_init(void);
void cabin_show_message(const char* title, uint32_t color_hex);
/** Full-screen busy state with spinner (logging start/stop). */
void cabin_show_busy(const char* title, uint32_t color_hex);
void cabin_show_setup(const char* ip_or_null);
void cabin_show_page(uint8_t index);
uint8_t cabin_page_count(void);
void cabin_refresh_info(int lipo_mv, bool lipo_ok, size_t free_kb);
void cabin_refresh_live(const GpsFix* fix, bool logging, uint32_t wifi_total, uint32_t ble_total,
                        uint32_t wifi_session, uint32_t ble_session);
void cabin_refresh_recent(void);
