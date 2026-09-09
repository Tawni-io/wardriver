#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifndef USE_TOUCH_SWIPE
#define USE_TOUCH_SWIPE 0
#endif

enum TouchNav : uint8_t {
  kTouchNavNone = 0,
  kTouchNavNext = 1,
  kTouchNavPrev = 2,
};

/** True if AXP2602 probed OK (board PMIC present). Cached at touch_init. */
bool board_power_present(void);

/**
 * Read board LiPo voltage (mV), optional SoC (%), optional battery current (mA).
 * Returns false if PMIC missing or I2C read fails.
 * soc_pct: written -1 when not a plausible 0–100 reading (pass nullptr to skip).
 * i_mA: signed; positive ≈ charging (USB), negative ≈ discharging (LiPo). Pass nullptr to skip.
 */
bool board_power_read(int* mV, int* soc_pct, int* i_mA);

#if USE_TOUCH_SWIPE

/** Init I2C, AXP probe, CST816S; prints scan. Safe once from setup. */
bool touch_init(void);

/** When true (upside-down mount), swipe next/prev are inverted. */
void touch_set_nav_flip(bool flip);

/** Drain one gesture if any. Returns next/prev nav or none. */
TouchNav touch_poll(void);

/** True after driver armed at boot. */
bool touch_armed(void);

/** True after first successful CST816S read. */
bool touch_confirmed(void);

/** One-line status for hb / late serial (INT, confirmed, probe0x15, int_edges). */
void touch_print_status(void);

#else

inline bool touch_init(void) { return false; }
inline void touch_set_nav_flip(bool) {}
inline TouchNav touch_poll(void) { return kTouchNavNone; }
inline bool touch_armed(void) { return false; }
inline bool touch_confirmed(void) { return false; }
inline void touch_print_status(void) {}

#endif
