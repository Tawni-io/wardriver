#pragma once

#include <stdint.h>
#include <stdbool.h>

/** Cabin layout: 0 = portrait (default), 1 = landscape. SoftAP save + reboot. */
constexpr uint8_t kOrientPortrait = 0;
constexpr uint8_t kOrientLandscape = 1;

bool prefs_begin(void);
uint8_t prefs_get_orient(void);
bool prefs_set_orient(uint8_t orient);
/** Reset display prefs (orient → portrait). Factory reset path. */
void prefs_reset_display(void);
/** Wipe all Wardriver NVS keys and reboot-friendly defaults. */
void prefs_factory_reset(void);
