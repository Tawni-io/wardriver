#pragma once

#include <stdint.h>

/** TAWNI mark + Wardriver version from TAWNI_VERSION (release bumps). After lvgl_port_init(). */
void splash_show(void);
/** Bottom GPS/wake status only — never shares the version label. */
void splash_set_status(const char* text, uint32_t color_hex);
/** Free splash screen after cabin UI is loaded. */
void splash_discard(void);
