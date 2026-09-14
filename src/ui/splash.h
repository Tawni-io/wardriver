#pragma once

#include <stdint.h>

/** TAWNI mark on black. Call after lvgl_port_init(); pumps a full refresh. */
void splash_show(void);
/** Status line under the mark (wake-confirm GPS probe). */
void splash_set_status(const char* text, uint32_t color_hex);
/** Free splash screen after cabin UI is loaded. */
void splash_discard(void);
