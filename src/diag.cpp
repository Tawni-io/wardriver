#include "diag.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ui/lvgl_port.h"

namespace {

volatile DiagStage g_stage = kDiagIdle;
volatile int g_fx = 0, g_fy = 0, g_fw = 0, g_fh = 0;
bool g_wdt_ok = false;

#ifndef VICTRONDASH_TASK_WDT_MS
#define VICTRONDASH_TASK_WDT_MS 12000
#endif

}  // namespace

void diag_begin(void) {
  g_stage = kDiagIdle;

#if VICTRONDASH_TASK_WDT_MS > 0
  // Panic on timeout so USB gets a backtrace (use debug build + exception decoder).
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = VICTRONDASH_TASK_WDT_MS;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;

  esp_err_t err = esp_task_wdt_reconfigure(&cfg);
  if (err == ESP_ERR_INVALID_STATE) {
    err = esp_task_wdt_init(&cfg);
  }
  if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
    err = esp_task_wdt_add(nullptr);
    g_wdt_ok = (err == ESP_OK || err == ESP_ERR_INVALID_STATE);
  }
  Serial.printf("DIAG: task WDT %s (%d ms, panic)\n", g_wdt_ok ? "ON" : "OFF",
                VICTRONDASH_TASK_WDT_MS);
#else
  Serial.println("DIAG: task WDT disabled");
#endif

  diag_print_mem("boot");
}

void diag_set_stage(DiagStage stage) {
  g_stage = stage;
}

DiagStage diag_stage(void) {
  return g_stage;
}

const char* diag_stage_name(DiagStage stage) {
  switch (stage) {
    case kDiagIdle:
      return "IDLE";
    case kDiagBtn:
      return "BTN";
    case kDiagLvEnter:
      return "LV_IN";
    case kDiagLvLeave:
      return "LV_OUT";
    case kDiagBle:
      return "BLE";
    case kDiagDecode:
      return "DEC";
    case kDiagUiEnter:
      return "UI_IN";
    case kDiagUiLeave:
      return "UI_OUT";
    case kDiagFlushSwap:
      return "FL_SWAP";
    case kDiagFlushDraw:
      return "FL_DRAW";
    case kDiagFlushWait:
      return "FL_WAIT";
    case kDiagFlushReady:
      return "FL_RDY";
    case kDiagSoftAp:
      return "AP";
    default:
      return "?";
  }
}

void diag_note_flush_rect(int x, int y, int w, int h) {
  g_fx = x;
  g_fy = y;
  g_fw = w;
  g_fh = h;
}

void diag_print_mem(const char* tag) {
  const size_t heap = ESP.getFreeHeap();
  const size_t min_heap = ESP.getMinFreeHeap();
  const size_t max_block = ESP.getMaxAllocHeap();
  const size_t internal =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const size_t dma = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  const UBaseType_t stack_hwm = uxTaskGetStackHighWaterMark(nullptr);

  Serial.printf(
      "MEM %s stg=%s heap=%u min=%u maxblk=%u int=%u dma=%u stk=%u "
      "flush=%u isr=%u to=%u rect=%d,%d %dx%d\n",
      tag ? tag : "-", diag_stage_name(g_stage), (unsigned)heap, (unsigned)min_heap,
      (unsigned)max_block, (unsigned)internal, (unsigned)dma, (unsigned)stack_hwm,
      (unsigned)lvgl_port_flush_enter_count(), (unsigned)lvgl_port_dma_isr_count(),
      (unsigned)lvgl_port_dma_timeout_count(), (int)g_fx, (int)g_fy, (int)g_fw, (int)g_fh);
}

void diag_wdt_feed(void) {
  if (g_wdt_ok) {
    esp_task_wdt_reset();
  }
}

uint32_t diag_flush_x(void) { return (uint32_t)g_fx; }
uint32_t diag_flush_y(void) { return (uint32_t)g_fy; }
uint32_t diag_flush_w(void) { return (uint32_t)g_fw; }
uint32_t diag_flush_h(void) { return (uint32_t)g_fh; }
