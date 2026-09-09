#include "ui/lvgl_port.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lvgl.h>

#include "board_pins.h"
#include "diag.h"
#include "display.h"

namespace {

bool g_ready = false;
lv_display_t* g_disp = nullptr;
SemaphoreHandle_t g_dma_done = nullptr;
void* g_draw_buf = nullptr;
bool g_bufs_suspended = false;

portMUX_TYPE g_cnt_mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t g_dma_isr_count = 0;
uint32_t g_flush_enter_count = 0;
uint32_t g_dma_timeouts = 0;

constexpr int kBufLines = 20;
constexpr TickType_t kDmaWaitTicks = pdMS_TO_TICKS(500);

uint32_t tick_cb(void) {
  return millis();
}

bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*,
                                   void*) {
  BaseType_t hp = pdFALSE;
  portENTER_CRITICAL_ISR(&g_cnt_mux);
  g_dma_isr_count += 1;
  portEXIT_CRITICAL_ISR(&g_cnt_mux);
  if (g_dma_done) {
    xSemaphoreGiveFromISR(g_dma_done, &hp);
  }
  return hp == pdTRUE;
}

void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  if (g_bufs_suspended || !px_map) {
    lv_display_flush_ready(disp);
    return;
  }
  auto panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
  const int x1 = area->x1;
  const int x2 = area->x2;
  const int y1 = area->y1;
  const int y2 = area->y2;
  const int w = x2 - x1 + 1;
  const int h = y2 - y1 + 1;

  portENTER_CRITICAL(&g_cnt_mux);
  g_flush_enter_count += 1;
  portEXIT_CRITICAL(&g_cnt_mux);

  diag_note_flush_rect(x1, y1, w, h);
  diag_set_stage(kDiagFlushSwap);
  diag_wdt_feed();

  lv_draw_sw_rgb565_swap(px_map, w * h);

  if (g_dma_done) {
    while (xSemaphoreTake(g_dma_done, 0) == pdTRUE) {
    }
  }

  diag_set_stage(kDiagFlushDraw);
  esp_err_t err = esp_lcd_panel_draw_bitmap(panel, x1, y1, x2 + 1, y2 + 1, px_map);
  if (err != ESP_OK) {
    diag_set_stage(kDiagFlushReady);
    lv_display_flush_ready(disp);
    return;
  }

  diag_set_stage(kDiagFlushWait);
  if (g_dma_done) {
    if (xSemaphoreTake(g_dma_done, kDmaWaitTicks) != pdTRUE) {
      g_dma_timeouts++;
    }
  }

  diag_set_stage(kDiagFlushReady);
  diag_wdt_feed();
  lv_display_flush_ready(disp);
}

}  // namespace

bool lvgl_port_init(void) {
  if (g_ready) {
    return true;
  }

  esp_lcd_panel_handle_t panel = display_panel();
  if (!panel) {
    Serial.println("LVGL: no panel");
    return false;
  }

  g_dma_done = xSemaphoreCreateBinary();
  if (!g_dma_done) {
    Serial.println("LVGL: DMA semaphore OOM");
    return false;
  }

  lv_init();
  lv_tick_set_cb(tick_cb);

  g_disp = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  if (!g_disp) {
    Serial.println("LVGL: display create failed");
    return false;
  }

  lv_display_set_rotation(g_disp, LV_DISPLAY_ROTATION_0);
  lv_display_set_user_data(g_disp, panel);
  lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(g_disp, flush_cb);

  if (!display_bind_color_done_cb(on_color_trans_done, nullptr)) {
    Serial.println("LVGL: bind color_done FAILED");
    return false;
  }

  const size_t buf_sz = (size_t)DISPLAY_WIDTH * (size_t)kBufLines * sizeof(lv_color_t);
  g_draw_buf = heap_caps_malloc(buf_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!g_draw_buf) {
    Serial.printf("LVGL: draw buffer OOM (need %u bytes DMA)\n", (unsigned)buf_sz);
    return false;
  }

  lv_display_set_buffers(g_disp, g_draw_buf, nullptr, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
  g_bufs_suspended = false;
  g_ready = true;
  Serial.printf("LVGL ready (partial %d lines, blocking-DMA, free heap %u)\n", kBufLines,
                (unsigned)ESP.getFreeHeap());
  return true;
}

bool lvgl_port_suspend_draw_buf(void) {
  if (!g_ready || g_bufs_suspended) {
    return true;
  }
  // SoftAP does not paint — return the 12.8KB INTERNAL+DMA block to the heap so
  // Wi‑Fi/HTTP can reassemble after Flip / leave / re-enter without a power cycle.
  // Keep a tiny static buffer registered so LVGL never sees a NULL draw buffer.
  static lv_color_t s_tiny[DISPLAY_WIDTH];
  if (g_disp) {
    lv_display_set_buffers(g_disp, s_tiny, nullptr, sizeof(s_tiny),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
  }
  if (g_draw_buf) {
    heap_caps_free(g_draw_buf);
    g_draw_buf = nullptr;
  }
  g_bufs_suspended = true;
  Serial.printf("LVGL: draw buf suspended (heap %u maxblk %u)\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
  return true;
}

bool lvgl_port_resume_draw_buf(void) {
  if (!g_ready || !g_bufs_suspended) {
    return true;
  }
  const size_t buf_sz = (size_t)DISPLAY_WIDTH * (size_t)kBufLines * sizeof(lv_color_t);
  g_draw_buf = heap_caps_malloc(buf_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!g_draw_buf) {
    Serial.printf("LVGL: draw buf resume OOM (need %u)\n", (unsigned)buf_sz);
    return false;
  }
  lv_display_set_buffers(g_disp, g_draw_buf, nullptr, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
  g_bufs_suspended = false;
  Serial.printf("LVGL: draw buf resumed (heap %u maxblk %u)\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
  return true;
}

void lvgl_port_handler(void) {
  if (!g_ready || g_bufs_suspended) return;

  diag_set_stage(kDiagLvEnter);
  // Cap spam: SoftAP→DECODE path only needs a few LV_IN/OUT pairs.
  static uint32_t s_lv_logs = 0;
  static bool s_verbose = true;
  if (s_verbose && s_lv_logs < 12) {
    diag_print_mem("LV_IN");
  }

  diag_wdt_feed();
  lv_timer_handler();
  diag_wdt_feed();

  diag_set_stage(kDiagLvLeave);
  if (s_verbose && s_lv_logs < 12) {
    diag_print_mem("LV_OUT");
    s_lv_logs++;
    if (s_lv_logs >= 12) {
      s_verbose = false;
      Serial.println("DIAG: LV mem logs capped (still staging + WDT)");
    }
  }
}

bool lvgl_port_ready(void) {
  return g_ready;
}

uint32_t lvgl_port_dma_isr_count(void) {
  return g_dma_isr_count;
}

uint32_t lvgl_port_flush_enter_count(void) {
  return g_flush_enter_count;
}

uint32_t lvgl_port_dma_timeout_count(void) {
  return g_dma_timeouts;
}
