#include "display.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_st7789.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "board_pins.h"
#include "config/prefs.h"

namespace {

constexpr int kSpiClockHz = 40000000;  // LilyGO factory clock

esp_lcd_panel_handle_t g_panel = nullptr;
esp_lcd_panel_io_handle_t g_io = nullptr;
bool g_ready = false;
int g_disp_w = LCD_WIDTH;
int g_disp_h = LCD_HEIGHT;
bool g_landscape = false;

esp_lcd_panel_io_color_trans_done_cb_t g_color_done_cb = nullptr;
void* g_color_done_ctx = nullptr;
SemaphoreHandle_t g_blit_done = nullptr;

// Registered on SPI IO at create time; forwards to LVGL once bound.
bool IRAM_ATTR color_done_trampoline(esp_lcd_panel_io_handle_t io,
                                     esp_lcd_panel_io_event_data_t* edata, void*) {
  if (g_color_done_cb) {
    return g_color_done_cb(io, edata, g_color_done_ctx);
  }
  BaseType_t hp = pdFALSE;
  if (g_blit_done) {
    xSemaphoreGiveFromISR(g_blit_done, &hp);
  }
  return hp == pdTRUE;
}

void blit_wait(void) {
  if (g_color_done_cb || !g_blit_done) {
    delay(2);
    return;
  }
  xSemaphoreTake(g_blit_done, pdMS_TO_TICKS(200));
}

void blit_drain(void) {
  if (!g_blit_done || g_color_done_cb) {
    return;
  }
  while (xSemaphoreTake(g_blit_done, 0) == pdTRUE) {
  }
}

// Panel wants big-endian RGB565 on the wire (LilyGO LVGL path).
inline uint16_t to_panel_color(uint16_t rgb565) {
  return (uint16_t)((rgb565 << 8) | (rgb565 >> 8));
}

// Portrait: native 170×320. Landscape: factory MADCTL swap (320×170).
bool panel_apply_layout(bool landscape) {
  if (!g_panel) {
    return false;
  }
  if (landscape) {
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(g_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(g_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(g_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(g_panel, 0, 35));
    g_disp_w = LCD_HEIGHT;
    g_disp_h = LCD_WIDTH;
  } else {
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(g_panel, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(g_panel, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(g_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(g_panel, 35, 0));
    g_disp_w = LCD_WIDTH;
    g_disp_h = LCD_HEIGHT;
  }
  g_landscape = landscape;
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(g_panel, true));
  return true;
}

}  // namespace

bool display_bind_color_done_cb(esp_lcd_panel_io_color_trans_done_cb_t cb, void* user_ctx) {
  g_color_done_cb = cb;
  g_color_done_ctx = user_ctx;
  // Also re-register via API in case create-time slot was ignored.
  if (g_io && cb) {
    esp_lcd_panel_io_callbacks_t cbs = {};
    cbs.on_color_trans_done = color_done_trampoline;
    esp_err_t err = esp_lcd_panel_io_register_event_callbacks(g_io, &cbs, nullptr);
    if (err != ESP_OK) {
      Serial.printf("display: register color_done failed: %s\n", esp_err_to_name(err));
      return false;
    }
  }
  return true;
}

bool display_init(void) {
  if (g_ready) {
    return true;
  }

  pinMode(LCD_BLK_POWER, OUTPUT);
  digitalWrite(LCD_BLK_POWER, HIGH);

  spi_bus_config_t bus_config = {};
  bus_config.mosi_io_num = LCD_MOSI;
  bus_config.miso_io_num = -1;
  bus_config.sclk_io_num = LCD_SCK;
  bus_config.quadwp_io_num = -1;
  bus_config.quadhd_io_num = -1;
  bus_config.max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2 + 8;

  esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    Serial.printf("SPI init failed: %s\n", esp_err_to_name(err));
    return false;
  }

  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.cs_gpio_num = LCD_CS;
  io_config.dc_gpio_num = LCD_DC;
  io_config.spi_mode = 0;
  io_config.pclk_hz = kSpiClockHz;
  // Depth 1: one in-flight color DMA at a time (safer with blocking LVGL flush).
  io_config.trans_queue_depth = 1;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  // DMA-done trampoline → LVGL semaphore (bound in lvgl_port_init).
  io_config.on_color_trans_done = color_done_trampoline;
  io_config.user_ctx = nullptr;

  err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &g_io);
  if (err != ESP_OK) {
    Serial.printf("panel IO failed: %s\n", esp_err_to_name(err));
    return false;
  }

  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = -1;
  panel_config.rgb_endian = LCD_RGB_ENDIAN_BGR;
  panel_config.bits_per_pixel = 16;

  err = esp_lcd_new_panel_st7789(g_io, &panel_config, &g_panel);
  if (err != ESP_OK) {
    Serial.printf("ST7789 create failed: %s\n", esp_err_to_name(err));
    return false;
  }

  pinMode(LCD_RST, OUTPUT);
  digitalWrite(LCD_RST, HIGH);
  delay(25);
  digitalWrite(LCD_RST, LOW);
  delay(25);
  digitalWrite(LCD_RST, HIGH);
  delay(125);

  ESP_ERROR_CHECK(esp_lcd_panel_init(g_panel));
  if (!panel_apply_layout(false)) {
    return false;
  }

  if (!g_blit_done) {
    g_blit_done = xSemaphoreCreateBinary();
  }

  g_ready = true;
  display_fill(COL_BLACK);
  Serial.printf("LCD ready (%dx%d %s)\n", g_disp_w, g_disp_h,
                g_landscape ? "landscape" : "portrait");
  return true;
}

esp_lcd_panel_handle_t display_panel(void) {
  return g_panel;
}

esp_lcd_panel_io_handle_t display_panel_io(void) {
  return g_io;
}

int display_width(void) { return g_disp_w; }

int display_height(void) { return g_disp_h; }

bool display_is_landscape(void) { return g_landscape; }

#include "config/prefs.h"

bool display_apply_layout(uint8_t layout) {
  const bool landscape = (layout == kOrientLandscape);
  const bool ok = panel_apply_layout(landscape);
  if (ok) {
    Serial.printf("LCD layout %s (%dx%d)\n", landscape ? "landscape" : "portrait", g_disp_w,
                  g_disp_h);
  }
  return ok;
}

void display_set_backlight(bool on) {
  digitalWrite(LCD_BLK_POWER, on ? HIGH : LOW);
}

void display_enter_sleep(void) {
  display_set_backlight(false);
  if (!g_panel) {
    return;
  }
  // Prefer panel sleep (SLPIN); fall back to display-off.
  if (esp_lcd_panel_disp_sleep(g_panel, true) != ESP_OK) {
    esp_lcd_panel_disp_on_off(g_panel, false);
  }
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color_rgb565) {
  if (!g_ready || w <= 0 || h <= 0) {
    return;
  }

  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > g_disp_w) {
    w = g_disp_w - x;
  }
  if (y + h > g_disp_h) {
    h = g_disp_h - y;
  }
  if (w <= 0 || h <= 0) {
    return;
  }

  // Full-width strips reduce edge artifacts vs tall narrow blits
  constexpr int kStripH = 8;
  const uint16_t panel_color = to_panel_color(color_rgb565);
  uint16_t* buf =
      (uint16_t*)heap_caps_malloc((size_t)w * kStripH * sizeof(uint16_t), MALLOC_CAP_DMA);
  if (!buf) {
    Serial.println("display_fill_rect: OOM");
    return;
  }

  blit_drain();
  for (int row = 0; row < h; row += kStripH) {
    const int bh = (row + kStripH <= h) ? kStripH : (h - row);
    const size_t pixels = (size_t)w * (size_t)bh;
    for (size_t i = 0; i < pixels; i++) {
      buf[i] = panel_color;
    }
    esp_lcd_panel_draw_bitmap(g_panel, x, y + row, x + w, y + row + bh, buf);
    blit_wait();
  }

  free(buf);
}

void display_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color_rgb565) {
  if (w <= 0 || h <= 0) return;
  if (r < 1) {
    display_fill_rect(x, y, w, h, color_rgb565);
    return;
  }
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;

  display_fill_rect(x + r, y, w - 2 * r, h, color_rgb565);
  display_fill_rect(x, y + r, r, h - 2 * r, color_rgb565);
  display_fill_rect(x + w - r, y + r, r, h - 2 * r, color_rgb565);

  // Cheap rounded corners (stepped)
  for (int i = 0; i < r; i++) {
    const int inset = r - 1 - i;
    const int len = r - inset;
    if (len <= 0) continue;
    display_fill_rect(x + inset, y + i, len, 1, color_rgb565);
    display_fill_rect(x + w - inset - len, y + i, len, 1, color_rgb565);
    display_fill_rect(x + inset, y + h - 1 - i, len, 1, color_rgb565);
    display_fill_rect(x + w - inset - len, y + h - 1 - i, len, 1, color_rgb565);
  }
}

void display_fill(uint16_t color_rgb565) {
  display_fill_rect(0, 0, g_disp_w, g_disp_h, color_rgb565);
}

void display_draw_bitmap(int x, int y, int w, int h, const uint16_t* rgb565) {
  if (!g_ready || !rgb565 || w <= 0 || h <= 0) {
    return;
  }

  const int stride = w;
  int src_x = 0;
  int src_y = 0;
  if (x < 0) {
    src_x = -x;
    w += x;
    x = 0;
  }
  if (y < 0) {
    src_y = -y;
    h += y;
    y = 0;
  }
  if (x + w > g_disp_w) {
    w = g_disp_w - x;
  }
  if (y + h > g_disp_h) {
    h = g_disp_h - y;
  }
  if (w <= 0 || h <= 0) {
    return;
  }

  // Full-width strips match the known-good fill path on this ST7789.
  constexpr int kStripH = 8;
  uint16_t* buf = (uint16_t*)heap_caps_malloc(
      (size_t)g_disp_w * kStripH * sizeof(uint16_t), MALLOC_CAP_DMA);
  if (!buf) {
    Serial.println("display_draw_bitmap: OOM");
    return;
  }

  const uint16_t pad = to_panel_color(COL_BLACK);
  blit_drain();
  for (int row = 0; row < h; row += kStripH) {
    const int bh = (row + kStripH <= h) ? kStripH : (h - row);
    for (int dy = 0; dy < bh; dy++) {
      uint16_t* dst = buf + (size_t)dy * (size_t)g_disp_w;
      for (int col = 0; col < g_disp_w; col++) {
        dst[col] = pad;
      }
      const uint16_t* src =
          rgb565 + (size_t)(src_y + row + dy) * (size_t)stride + (size_t)src_x;
      for (int col = 0; col < w; col++) {
        dst[x + col] = to_panel_color(src[col]);
      }
    }
    esp_lcd_panel_draw_bitmap(g_panel, 0, y + row, g_disp_w, y + row + bh, buf);
    blit_wait();
  }

  free(buf);
}
