#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

bool display_init(void);
esp_lcd_panel_handle_t display_panel(void);
esp_lcd_panel_io_handle_t display_panel_io(void);

// Bind LVGL flush_ready to SPI color-transfer-done (IO trampoline set at create).
bool display_bind_color_done_cb(esp_lcd_panel_io_color_trans_done_cb_t cb, void* user_ctx);
uint8_t display_preset_count(void);
bool display_reconfigure(uint8_t preset_id);
void display_set_backlight(bool on);
/** Backlight off + ST7789 sleep (soft power-off prep). */
void display_enter_sleep(void);
void display_fill(uint16_t color_rgb565);
void display_fill_rect(int x, int y, int w, int h, uint16_t color_rgb565);
void display_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color_rgb565);
/** Host-order RGB565 bitmap; swapped for the panel on draw. */
void display_draw_bitmap(int x, int y, int w, int h, const uint16_t* rgb565);

// RGB565 helpers (host order — swapped for panel on draw)
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Cabin palette — inspired by LilyGO T-Display-C5 factory dashboard
static constexpr uint16_t COL_BG     = rgb565(0x0D, 0x11, 0x17);
static constexpr uint16_t COL_CARD   = rgb565(0x16, 0x1B, 0x22);
static constexpr uint16_t COL_DIM    = rgb565(0x30, 0x36, 0x3D);
static constexpr uint16_t COL_FG     = rgb565(0xFF, 0xFF, 0xFF);
static constexpr uint16_t COL_MUTED  = rgb565(0x8B, 0x94, 0x9E);
static constexpr uint16_t COL_OK     = rgb565(0x00, 0xE6, 0x76);
static constexpr uint16_t COL_WARN   = rgb565(0xFF, 0xD6, 0x00);
static constexpr uint16_t COL_ALARM  = rgb565(0xFF, 0x17, 0x44);
static constexpr uint16_t COL_CYAN   = rgb565(0x00, 0xBC, 0xD4);
static constexpr uint16_t COL_ORANGE = rgb565(0xFF, 0x91, 0x00);
static constexpr uint16_t COL_RED    = 0xF800;
static constexpr uint16_t COL_GREEN  = 0x07E0;
static constexpr uint16_t COL_BLUE   = 0x001F;
static constexpr uint16_t COL_WHITE  = 0xFFFF;
static constexpr uint16_t COL_BLACK  = 0x0000;
