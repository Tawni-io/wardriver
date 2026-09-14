#pragma once

#include <stdint.h>

// Crash/hang forensics: stage sticky + memory snapshots.
// If serial dies, last printed STG/MEM line is the smoking gun.
// Task WDT panic (if enabled) should produce a backtrace with debug build.

enum DiagStage : uint8_t {
  kDiagIdle = 0,
  kDiagBtn,
  kDiagLvEnter,
  kDiagLvLeave,
  kDiagBle,
  kDiagDecode,
  kDiagUiEnter,
  kDiagUiLeave,
  kDiagFlushSwap,
  kDiagFlushDraw,
  kDiagFlushWait,
  kDiagFlushReady,
  kDiagSoftAp,
};

void diag_begin(void);
void diag_set_stage(DiagStage stage);
DiagStage diag_stage(void);
const char* diag_stage_name(DiagStage stage);

// Call from flush_cb (no Serial): last strip coords for WDT/postmortem.
void diag_note_flush_rect(int x, int y, int w, int h);

// Print one MEM line: heap / internal / DMA-capable / stack HWM / flush stats / stage / rect.
void diag_print_mem(const char* tag);

// Feed task WDT (call each loop).
void diag_wdt_feed(void);

uint32_t diag_flush_x(void);
uint32_t diag_flush_y(void);
uint32_t diag_flush_w(void);
uint32_t diag_flush_h(void);
