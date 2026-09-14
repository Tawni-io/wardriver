#include "ui/cabin.h"

#include "board_pins.h"
#include "diag.h"
#include "display.h"
#include "gps/gps.h"
#include "log/wigle_log.h"
#include "scan/survey.h"
#include "ui/lvgl_port.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#ifndef TAWNI_VERSION
#define TAWNI_VERSION "0.0.0"
#endif

namespace {

constexpr uint32_t kBg = 0x0D1117;
constexpr uint32_t kCard = 0x161B22;
constexpr uint32_t kFg = 0xFFFFFF;
constexpr uint32_t kMuted = 0x8B949E;
constexpr uint32_t kDim = 0x30363D;
constexpr uint32_t kOk = 0x00E676;
constexpr uint32_t kCyan = 0x00BCD4;   /* chrome: PAGE, panel accents */
constexpr uint32_t kOrange = 0xFF9100;
constexpr uint32_t kPurple = 0x7C4DFF;  /* BLE identity */
constexpr uint32_t kWifi = 0x42A5F5;    /* Wi‑Fi identity (distinct from chrome cyan) */

constexpr int kPad = 6;
constexpr int kGap = 6;
/** Shared button-tile look (portrait bar + landscape rail). */
constexpr int kBtnRadius = 6;
constexpr int kBtnBarH = 30;   /* portrait: keep chrome slim */
constexpr int kRailW = 60;     /* landscape: room for PAGE ↑ */
constexpr int kRecentMaxUi = 16;
constexpr int kRecentRowH = 18;
constexpr int kRecentRssiW = 36;

lv_obj_t* scr_msg = nullptr;
lv_obj_t* msg_title = nullptr;
lv_obj_t* scr_busy = nullptr;
lv_obj_t* busy_title = nullptr;
lv_obj_t* busy_spinner = nullptr;
lv_obj_t* scr_setup = nullptr;
lv_obj_t* setup_ip = nullptr;
lv_obj_t* scr_live = nullptr;
lv_obj_t* live_sub = nullptr;
lv_obj_t* live_gps = nullptr;
lv_obj_t* live_coords = nullptr;
lv_obj_t* live_wifi_total = nullptr;
lv_obj_t* live_wifi_session = nullptr;
lv_obj_t* live_ble_total = nullptr;
lv_obj_t* live_ble_session = nullptr;
lv_obj_t* live_btn_bot = nullptr;
lv_obj_t* recent_btn_bot = nullptr;
lv_obj_t* info_btn_bot = nullptr;
lv_obj_t* scr_recent = nullptr;
lv_obj_t* recent_list = nullptr;
lv_obj_t* recent_names[kRecentMaxUi] = {};
lv_obj_t* recent_rssis[kRecentMaxUi] = {};
int recent_line_n = 0;
uint32_t recent_seen_gen = 0;
lv_obj_t* scr_info = nullptr;
lv_obj_t* info_lipo = nullptr;
lv_obj_t* info_fs = nullptr;

bool is_landscape(void) { return display_is_landscape(); }

lv_obj_t* make_screen(void) {
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(kBg), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_pad_all(l, 0, 0);
  return l;
}

/** Same language as content tiles: rounded, accent border, solid tint fill. */
lv_obj_t* make_btn_tile(lv_obj_t* parent, int x, int y, int w, int h, uint32_t accent) {
  lv_obj_t* tile = lv_obj_create(parent);
  lv_obj_set_pos(tile, x, y);
  lv_obj_set_size(tile, w, h);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(tile, kBtnRadius, 0);
  lv_obj_set_style_bg_color(tile, lv_color_hex(accent), 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_70, 0);
  lv_obj_set_style_border_width(tile, 1, 0);
  lv_obj_set_style_border_color(tile, lv_color_hex(accent), 0);
  lv_obj_set_style_border_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(tile, 0, 0);
  return tile;
}

void style_hint_short(lv_obj_t* lbl) {
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(kFg), 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(lbl, LV_SIZE_CONTENT);
}

void style_hint_rail(lv_obj_t* lbl) {
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(kFg), 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, LV_SIZE_CONTENT);
}

lv_obj_t* make_accent_panel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t accent) {
  lv_obj_t* tile = lv_obj_create(parent);
  lv_obj_set_pos(tile, x, y);
  lv_obj_set_size(tile, w, h);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(tile, kBtnRadius, 0);
  lv_obj_set_style_bg_color(tile, lv_color_hex(kCard), 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tile, 1, 0);
  lv_obj_set_style_border_color(tile, lv_color_hex(accent), 0);
  lv_obj_set_style_pad_all(tile, 6, 0);
  return tile;
}

void make_count_tile(lv_obj_t* parent, int x, int y, int w, int h, const char* icon_title,
                     uint32_t accent, lv_obj_t** out_total, lv_obj_t** out_session) {
  lv_obj_t* tile = make_accent_panel(parent, x, y, w, h, accent);
  const int row = (h >= 56) ? 22 : 16;
  const lv_font_t* title_font = (h >= 56) ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
  const lv_font_t* body_font = (h >= 56) ? &lv_font_montserrat_14 : &lv_font_montserrat_12;

  lv_obj_t* title = make_label(tile, title_font, accent);
  lv_label_set_text(title, icon_title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t* sess = make_label(tile, body_font, kMuted);
  lv_label_set_text(sess, "Session: 0");
  lv_obj_set_width(sess, w - 12);
  lv_label_set_long_mode(sess, LV_LABEL_LONG_CLIP);
  lv_obj_align(sess, LV_ALIGN_TOP_LEFT, 0, row);
  *out_session = sess;

  lv_obj_t* tot = make_label(tile, body_font, kFg);
  lv_label_set_text(tot, "Total: 0");
  lv_obj_set_width(tot, w - 12);
  lv_label_set_long_mode(tot, LV_LABEL_LONG_CLIP);
  lv_obj_align(tot, LV_ALIGN_TOP_LEFT, 0, row * 2);
  *out_total = tot;
}

/**
 * One label per physical button. Functions never swap with orientation:
 * GPIO0 = START/STOP, GPIO28 = PAGE.
 * Portrait: bottom bar left=START, right=PAGE.
 * Landscape: side rail bottom=START, top=PAGE.
 */
void make_button_chrome(lv_obj_t* parent, lv_obj_t** out_start) {
  if (!parent) return;
  const int dw = display_width();
  const int dh = display_height();

  if (is_landscape()) {
    const int outer = kPad;
    const int gap = kGap;
    const int card_w = kRailW - 4;
    const int card_h = (dh - outer * 2 - gap) / 2;
    const int rail_x = dw - kRailW + (kRailW - card_w) / 2;
    lv_obj_t* top = make_btn_tile(parent, rail_x, outer, card_w, card_h, kCyan);
    lv_obj_t* bot = make_btn_tile(parent, rail_x, outer + card_h + gap, card_w, card_h, kOrange);

    lv_obj_t* page = lv_label_create(top);
    style_hint_rail(page);
    lv_label_set_text(page, "PAGE\n" LV_SYMBOL_UP);
    lv_obj_align(page, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* start = lv_label_create(bot);
    style_hint_rail(start);
    lv_label_set_text(start, "START");
    lv_obj_align(start, LV_ALIGN_CENTER, 0, 0);
    if (out_start) *out_start = start;
    return;
  }

  const int y = dh - kPad - kBtnBarH;
  const int card_w = (dw - kPad * 3) / 2;
  lv_obj_t* left = make_btn_tile(parent, kPad, y, card_w, kBtnBarH, kOrange);
  lv_obj_t* right = make_btn_tile(parent, kPad + card_w + kPad, y, card_w, kBtnBarH, kCyan);

  lv_obj_t* start = lv_label_create(left);
  style_hint_short(start);
  lv_label_set_text(start, "START");
  lv_obj_align(start, LV_ALIGN_CENTER, 0, 0);
  if (out_start) *out_start = start;

  lv_obj_t* page = lv_label_create(right);
  style_hint_short(page);
  lv_label_set_text(page, "PAGE " LV_SYMBOL_UP);
  lv_obj_align(page, LV_ALIGN_CENTER, 0, 0);
}

void set_start_stop_labels(bool logging) {
  const char* t = logging ? "STOP" : "START";
  if (live_btn_bot) lv_label_set_text(live_btn_bot, t);
  if (recent_btn_bot) lv_label_set_text(recent_btn_bot, t);
  if (info_btn_bot) lv_label_set_text(info_btn_bot, t);
}

void load_screen(lv_obj_t* scr) {
  if (scr) lv_screen_load(scr);
}

/** Content column width left of landscape rail, or full portrait width. */
int content_w(void) {
  if (is_landscape()) return display_width() - kRailW - kPad * 2;
  return display_width() - kPad * 2;
}

int content_bottom_y(void) {
  if (is_landscape()) return display_height() - kPad;
  return display_height() - kPad - kBtnBarH - kGap;
}

void build_message_screen(void) {
  scr_msg = make_screen();
  msg_title = make_label(scr_msg, &lv_font_montserrat_20, kOk);
  lv_label_set_text(msg_title, "");
  lv_label_set_long_mode(msg_title, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(msg_title, display_width() - 24);
  lv_obj_set_style_text_align(msg_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(msg_title, LV_ALIGN_CENTER, 0, 0);
}

void build_busy_screen(void) {
  scr_busy = make_screen();
  busy_title = make_label(scr_busy, &lv_font_montserrat_14, kFg);
  lv_label_set_text(busy_title, "");
  lv_label_set_long_mode(busy_title, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(busy_title, display_width() - 24);
  lv_obj_set_style_text_align(busy_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(busy_title, LV_ALIGN_CENTER, 0, -28);

  busy_spinner = lv_spinner_create(scr_busy);
  lv_obj_set_size(busy_spinner, 42, 42);
  lv_obj_align(busy_spinner, LV_ALIGN_CENTER, 0, 28);
  lv_spinner_set_anim_params(busy_spinner, 1000, 200);
  lv_obj_set_style_arc_color(busy_spinner, lv_color_hex(kDim), LV_PART_MAIN);
  lv_obj_set_style_arc_color(busy_spinner, lv_color_hex(kOk), LV_PART_INDICATOR);
}

void make_setup_hold_hint(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* panel = make_accent_panel(parent, x, y, w, h, kOrange);
  lv_obj_set_style_pad_ver(panel, 2, 0);
  lv_obj_set_style_pad_hor(panel, 6, 0);
  lv_obj_t* hint = make_label(panel, &lv_font_montserrat_12, kMuted);
  lv_label_set_text(hint, LV_SYMBOL_SETTINGS " Hold Start 2s for setup");
  lv_label_set_long_mode(hint, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(hint, w - 12);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(hint, LV_ALIGN_LEFT_MID, 0, 0);
}

void build_setup_screen(void) {
  scr_setup = make_screen();
  const int dw = display_width();
  const int dh = display_height();
  const int outer = 8;
  const int gap = 6;

  if (is_landscape()) {
    const int card_h = dh - outer * 2;
    const int left_w = 150;
    const int right_x = outer + left_w + gap;
    const int right_w = dw - right_x - outer;

    lv_obj_t* left = make_accent_panel(scr_setup, outer, outer, left_w, card_h, kOk);
    lv_obj_t* t = make_label(left, &lv_font_montserrat_20, kOk);
    lv_label_set_text(t, LV_SYMBOL_SETTINGS " SETUP");
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* wifi = make_label(left, &lv_font_montserrat_14, kFg);
    lv_label_set_text(wifi, LV_SYMBOL_WIFI " TawniWardriver");
    lv_obj_set_width(wifi, left_w - 12);
    lv_label_set_long_mode(wifi, LV_LABEL_LONG_CLIP);
    lv_obj_align(wifi, LV_ALIGN_TOP_LEFT, 0, 32);

    setup_ip = make_label(left, &lv_font_montserrat_12, kCyan);
    lv_label_set_text(setup_ip, "http://192.168.4.1");
    lv_obj_set_width(setup_ip, left_w - 12);
    lv_label_set_long_mode(setup_ip, LV_LABEL_LONG_CLIP);
    lv_obj_align(setup_ip, LV_ALIGN_TOP_LEFT, 0, 54);

    lv_obj_t* join = make_label(left, &lv_font_montserrat_12, kMuted);
    lv_label_set_text(join, "Join on your phone");
    lv_obj_align(join, LV_ALIGN_TOP_LEFT, 0, 80);

    lv_obj_t* leave = make_label(left, &lv_font_montserrat_12, kMuted);
    lv_label_set_text(leave, "Hold Start to leave");
    lv_obj_align(leave, LV_ALIGN_TOP_LEFT, 0, 100);

    lv_obj_t* right = make_accent_panel(scr_setup, right_x, outer, right_w, card_h, kCyan);
    lv_obj_t* st = make_label(right, &lv_font_montserrat_14, kCyan);
    lv_label_set_text(st, "On this page");
    lv_obj_align(st, LV_ALIGN_TOP_LEFT, 0, 0);

    const char* services[] = {
        "1. Screen layout",
        "2. Wigle CSV",
        "3. GPS status",
        "4. Firmware OTA",
        "5. Factory reset",
    };
    for (int i = 0; i < 5; i++) {
      lv_obj_t* l = make_label(right, &lv_font_montserrat_12, kFg);
      lv_label_set_text(l, services[i]);
      lv_obj_set_width(l, right_w - 12);
      lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
      lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 26 + i * 18);
    }
  } else {
    const int top_h = 120;
    const int bot_y = outer + top_h + gap;
    const int bot_h = dh - bot_y - outer;
    const int w = dw - outer * 2;

    lv_obj_t* left = make_accent_panel(scr_setup, outer, outer, w, top_h, kOk);
    lv_obj_t* t = make_label(left, &lv_font_montserrat_20, kOk);
    lv_label_set_text(t, LV_SYMBOL_SETTINGS " SETUP");
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* wifi = make_label(left, &lv_font_montserrat_14, kFg);
    lv_label_set_text(wifi, LV_SYMBOL_WIFI " TawniWardriver");
    lv_obj_set_width(wifi, w - 12);
    lv_label_set_long_mode(wifi, LV_LABEL_LONG_CLIP);
    lv_obj_align(wifi, LV_ALIGN_TOP_LEFT, 0, 32);

    setup_ip = make_label(left, &lv_font_montserrat_12, kCyan);
    lv_label_set_text(setup_ip, "http://192.168.4.1");
    lv_obj_set_width(setup_ip, w - 12);
    lv_label_set_long_mode(setup_ip, LV_LABEL_LONG_WRAP);
    lv_obj_align(setup_ip, LV_ALIGN_TOP_LEFT, 0, 54);

    lv_obj_t* join = make_label(left, &lv_font_montserrat_12, kMuted);
    lv_label_set_text(join, "Join on your phone");
    lv_obj_align(join, LV_ALIGN_TOP_LEFT, 0, 78);

    lv_obj_t* leave = make_label(left, &lv_font_montserrat_12, kMuted);
    lv_label_set_text(leave, "Hold Start to leave");
    lv_obj_align(leave, LV_ALIGN_TOP_LEFT, 0, 96);

    lv_obj_t* right = make_accent_panel(scr_setup, outer, bot_y, w, bot_h, kCyan);
    lv_obj_t* st = make_label(right, &lv_font_montserrat_14, kCyan);
    lv_label_set_text(st, "On this page");
    lv_obj_align(st, LV_ALIGN_TOP_LEFT, 0, 0);

    const char* services[] = {
        "1. Screen layout",
        "2. Wigle CSV",
        "3. GPS status",
        "4. Firmware OTA",
        "5. Factory reset",
    };
    for (int i = 0; i < 5; i++) {
      lv_obj_t* l = make_label(right, &lv_font_montserrat_12, kFg);
      lv_label_set_text(l, services[i]);
      lv_obj_set_width(l, w - 12);
      lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
      lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 28 + i * 22);
    }
  }
}

void build_live_screen(void) {
  scr_live = make_screen();
  const int cw = content_w();
  const int bottom = content_bottom_y();
  const int inner_w = cw - 12;

  if (is_landscape()) {
    const int top_h = bottom - kPad;
    const int stats_w = 124;
    const int status_w = cw - kGap - stats_w;
    const int stats_x = kPad + status_w + kGap;
    const int stats_h = (top_h - kGap) / 2;

    lv_obj_t* status = make_accent_panel(scr_live, kPad, kPad, status_w, top_h, kOk);
    lv_obj_t* title = make_label(status, &lv_font_montserrat_20, kOk);
    lv_label_set_text(title, LV_SYMBOL_EYE_OPEN " LIVE");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    live_sub = make_label(status, &lv_font_montserrat_14, kMuted);
    lv_label_set_text(live_sub, LV_SYMBOL_STOP " Log OFF");
    lv_obj_set_width(live_sub, status_w - 12);
    lv_label_set_long_mode(live_sub, LV_LABEL_LONG_CLIP);
    lv_obj_align(live_sub, LV_ALIGN_TOP_LEFT, 0, 28);

    live_gps = make_label(status, &lv_font_montserrat_14, kFg);
    lv_label_set_text(live_gps, LV_SYMBOL_GPS " ...");
    lv_obj_set_width(live_gps, status_w - 12);
    lv_label_set_long_mode(live_gps, LV_LABEL_LONG_CLIP);
    lv_obj_align(live_gps, LV_ALIGN_TOP_LEFT, 0, 52);

    live_coords = make_label(status, &lv_font_montserrat_12, kMuted);
    lv_label_set_text(live_coords, "waiting for fix");
    lv_obj_set_width(live_coords, status_w - 12);
    lv_label_set_long_mode(live_coords, LV_LABEL_LONG_CLIP);
    lv_obj_align(live_coords, LV_ALIGN_TOP_LEFT, 0, 78);

    make_count_tile(scr_live, stats_x, kPad, stats_w, stats_h, LV_SYMBOL_WIFI " WiFi", kWifi,
                    &live_wifi_total, &live_wifi_session);
    make_count_tile(scr_live, stats_x, kPad + stats_h + kGap, stats_w, stats_h,
                    LV_SYMBOL_BLUETOOTH " BLE", kPurple, &live_ble_total, &live_ble_session);
  } else {
    /* Compact LIVE header; full-width Wi‑Fi above BLE. */
    const int status_h = 108;
    const int stack_top = kPad + status_h + kGap;
    const int stack_h = bottom - stack_top;
    const int tile_h = (stack_h - kGap) / 2;
    const int wifi_y = stack_top;
    const int ble_y = wifi_y + tile_h + kGap;

    lv_obj_t* status = make_accent_panel(scr_live, kPad, kPad, cw, status_h, kOk);
    lv_obj_t* title = make_label(status, &lv_font_montserrat_20, kOk);
    lv_label_set_text(title, LV_SYMBOL_EYE_OPEN " LIVE");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    live_sub = make_label(status, &lv_font_montserrat_14, kMuted);
    lv_label_set_text(live_sub, LV_SYMBOL_STOP " Log OFF");
    lv_obj_set_width(live_sub, inner_w);
    lv_label_set_long_mode(live_sub, LV_LABEL_LONG_CLIP);
    lv_obj_align(live_sub, LV_ALIGN_TOP_LEFT, 0, 26);

    live_gps = make_label(status, &lv_font_montserrat_14, kFg);
    lv_label_set_text(live_gps, LV_SYMBOL_GPS " ...");
    lv_obj_set_width(live_gps, inner_w);
    lv_label_set_long_mode(live_gps, LV_LABEL_LONG_CLIP);
    lv_obj_align(live_gps, LV_ALIGN_TOP_LEFT, 0, 48);

    live_coords = make_label(status, &lv_font_montserrat_12, kMuted);
    lv_label_set_text(live_coords, "waiting for fix");
    lv_obj_set_width(live_coords, inner_w);
    lv_label_set_long_mode(live_coords, LV_LABEL_LONG_WRAP);
    lv_obj_align(live_coords, LV_ALIGN_TOP_LEFT, 0, 70);

    make_count_tile(scr_live, kPad, wifi_y, cw, tile_h, LV_SYMBOL_WIFI " WiFi", kWifi,
                    &live_wifi_total, &live_wifi_session);
    make_count_tile(scr_live, kPad, ble_y, cw, tile_h, LV_SYMBOL_BLUETOOTH " BLE", kPurple,
                    &live_ble_total, &live_ble_session);
  }

  make_button_chrome(scr_live, &live_btn_bot);
}

void build_recent_screen(void) {
  scr_recent = make_screen();
  const int cw = content_w();
  const int bottom = content_bottom_y();
  const int main_h = bottom - kPad;
  recent_line_n = kRecentMaxUi;

  lv_obj_t* panel = make_accent_panel(scr_recent, kPad, kPad, cw, main_h, kCyan);
  lv_obj_t* title = make_label(panel, &lv_font_montserrat_20, kCyan);
  lv_label_set_text(title, LV_SYMBOL_LIST " RECENT");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t* hdr = make_label(panel, &lv_font_montserrat_12, kMuted);
  lv_label_set_text(hdr, "dBm");
  lv_obj_align(hdr, LV_ALIGN_TOP_RIGHT, 0, 4);

  recent_list = lv_obj_create(panel);
  lv_obj_set_pos(recent_list, 0, 26);
  lv_obj_set_size(recent_list, cw - 12, main_h - 38);
  lv_obj_set_style_bg_opa(recent_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(recent_list, 0, 0);
  lv_obj_set_style_pad_all(recent_list, 0, 0);
  lv_obj_set_style_radius(recent_list, 0, 0);
  lv_obj_set_scroll_dir(recent_list, LV_DIR_VER);
  lv_obj_add_flag(recent_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(recent_list, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_scrollbar_mode(recent_list, LV_SCROLLBAR_MODE_OFF);

  const int inner_w = cw - 12;
  const int name_w = inner_w - kRecentRssiW - 4;
  for (int i = 0; i < kRecentMaxUi; i++) {
    recent_names[i] = nullptr;
    recent_rssis[i] = nullptr;
  }
  for (int i = 0; i < recent_line_n; i++) {
    const int y = i * kRecentRowH;
    recent_names[i] = make_label(recent_list, &lv_font_montserrat_12, kMuted);
    lv_label_set_text(recent_names[i], "");
    lv_obj_set_width(recent_names[i], name_w);
    lv_label_set_long_mode(recent_names[i], LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(recent_names[i], 0, y);

    recent_rssis[i] = make_label(recent_list, &lv_font_montserrat_12, kMuted);
    lv_label_set_text(recent_rssis[i], "");
    lv_obj_set_width(recent_rssis[i], kRecentRssiW);
    lv_obj_set_style_text_align(recent_rssis[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(recent_rssis[i], LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(recent_rssis[i], name_w + 4, y);
  }

  make_button_chrome(scr_recent, &recent_btn_bot);
}

uint32_t rssi_color(int rssi) {
  if (rssi >= -50) return kOk;
  if (rssi >= -70) return kFg;
  return kMuted;
}

void build_info_screen(void) {
  scr_info = make_screen();
  const int cw = content_w();
  const int bottom = content_bottom_y();

  if (is_landscape()) {
    constexpr int kHintH = 26;
    const int hint_y = bottom - kHintH;
    const int main_h = hint_y - kPad - kGap;
    const int left_w = 148;
    const int right_x = kPad + left_w + kGap;
    const int right_w = cw - left_w - kGap;
    const int half_h = (main_h - kGap) / 2;

    lv_obj_t* device = make_accent_panel(scr_info, kPad, kPad, left_w, main_h, kCyan);
    lv_obj_t* title = make_label(device, &lv_font_montserrat_20, kCyan);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " INFO");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* ver = make_label(device, &lv_font_montserrat_14, kFg);
    lv_label_set_text(ver, "v" TAWNI_VERSION " Rufous");
    lv_obj_set_width(ver, left_w - 12);
    lv_label_set_long_mode(ver, LV_LABEL_LONG_CLIP);
    lv_obj_align(ver, LV_ALIGN_TOP_LEFT, 0, 28);

    lv_obj_t* Firmware = make_label(device, &lv_font_montserrat_12, kMuted);
    lv_label_set_text(Firmware, LV_SYMBOL_WIFI " TawniWardriver");
    lv_obj_set_width(Firmware, left_w - 12);
    lv_label_set_long_mode(Firmware, LV_LABEL_LONG_CLIP);
    lv_obj_align(Firmware, LV_ALIGN_TOP_LEFT, 0, 52);

    lv_obj_t* tip = make_label(device, &lv_font_montserrat_12, kMuted);
    lv_label_set_text(tip, "SoftAP CSV / OTA");
    lv_obj_align(tip, LV_ALIGN_TOP_LEFT, 0, 76);

    lv_obj_t* power = make_accent_panel(scr_info, right_x, kPad, right_w, half_h, kOk);
    lv_obj_t* ptitle = make_label(power, &lv_font_montserrat_12, kOk);
    lv_label_set_text(ptitle, LV_SYMBOL_BATTERY_FULL " Power");
    lv_obj_align(ptitle, LV_ALIGN_TOP_LEFT, 0, 0);
    info_lipo = make_label(power, &lv_font_montserrat_14, kFg);
    lv_label_set_text(info_lipo, "--");
    lv_obj_align(info_lipo, LV_ALIGN_TOP_LEFT, 0, 22);

    lv_obj_t* storage =
        make_accent_panel(scr_info, right_x, kPad + half_h + kGap, right_w, half_h, kPurple);
    lv_obj_t* stitle = make_label(storage, &lv_font_montserrat_12, kPurple);
    lv_label_set_text(stitle, LV_SYMBOL_SD_CARD " Log");
    lv_obj_align(stitle, LV_ALIGN_TOP_LEFT, 0, 0);
    info_fs = make_label(storage, &lv_font_montserrat_12, kFg);
    lv_label_set_text(info_fs, "--");
    lv_obj_set_width(info_fs, right_w - 12);
    lv_label_set_long_mode(info_fs, LV_LABEL_LONG_WRAP);
    lv_obj_align(info_fs, LV_ALIGN_TOP_LEFT, 0, 20);

    make_setup_hold_hint(scr_info, kPad, hint_y, cw, kHintH);
  } else {
    constexpr int kHintH = 26;
    const int hint_y = bottom - kHintH;
    const int stack_h = hint_y - kPad - kGap;
    const int row_h = 64;
    const int half_w = (cw - kGap) / 2;
    const int device_h = stack_h - kGap - row_h;
    const int row_y = kPad + device_h + kGap;
    const int device_inner = cw - 12;
    const int half_inner = half_w - 12;

    lv_obj_t* device = make_accent_panel(scr_info, kPad, kPad, cw, device_h, kCyan);
    lv_obj_t* title = make_label(device, &lv_font_montserrat_20, kCyan);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " INFO");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* ver = make_label(device, &lv_font_montserrat_14, kFg);
    lv_label_set_text(ver, "v" TAWNI_VERSION " Rufous");
    lv_obj_set_width(ver, device_inner);
    lv_label_set_long_mode(ver, LV_LABEL_LONG_WRAP);
    lv_obj_align(ver, LV_ALIGN_TOP_LEFT, 0, 28);

    lv_obj_t* ssid = make_label(device, &lv_font_montserrat_12, kMuted);
    lv_label_set_text(ssid, LV_SYMBOL_WIFI " TawniWardriver");
    lv_obj_set_width(ssid, device_inner);
    lv_label_set_long_mode(ssid, LV_LABEL_LONG_WRAP);
    lv_obj_align(ssid, LV_ALIGN_TOP_LEFT, 0, 56);

    lv_obj_t* tip = make_label(device, &lv_font_montserrat_12, kMuted);
    lv_label_set_text(tip, "SoftAP CSV / OTA / Orient");
    lv_obj_set_width(tip, device_inner);
    lv_label_set_long_mode(tip, LV_LABEL_LONG_WRAP);
    lv_obj_align(tip, LV_ALIGN_TOP_LEFT, 0, 88);

    lv_obj_t* power = make_accent_panel(scr_info, kPad, row_y, half_w, row_h, kOk);
    lv_obj_t* ptitle = make_label(power, &lv_font_montserrat_12, kOk);
    lv_label_set_text(ptitle, LV_SYMBOL_BATTERY_FULL " Power");
    lv_obj_align(ptitle, LV_ALIGN_TOP_LEFT, 0, 0);
    info_lipo = make_label(power, &lv_font_montserrat_14, kFg);
    lv_label_set_text(info_lipo, "--");
    lv_obj_set_width(info_lipo, half_inner);
    lv_label_set_long_mode(info_lipo, LV_LABEL_LONG_CLIP);
    lv_obj_align(info_lipo, LV_ALIGN_TOP_LEFT, 0, 22);

    lv_obj_t* storage =
        make_accent_panel(scr_info, kPad + half_w + kGap, row_y, half_w, row_h, kPurple);
    lv_obj_t* stitle = make_label(storage, &lv_font_montserrat_12, kPurple);
    lv_label_set_text(stitle, LV_SYMBOL_SD_CARD " Log");
    lv_obj_align(stitle, LV_ALIGN_TOP_LEFT, 0, 0);
    info_fs = make_label(storage, &lv_font_montserrat_12, kFg);
    lv_label_set_text(info_fs, "--");
    lv_obj_set_width(info_fs, half_inner);
    lv_label_set_long_mode(info_fs, LV_LABEL_LONG_WRAP);
    lv_obj_align(info_fs, LV_ALIGN_TOP_LEFT, 0, 22);

    make_setup_hold_hint(scr_info, kPad, hint_y, cw, kHintH);
  }

  make_button_chrome(scr_info, &info_btn_bot);
}

}  // namespace

bool cabin_init(void) {
  if (!lvgl_port_ready()) {
    return false;
  }
  build_message_screen();
  build_busy_screen();
  build_setup_screen();
  build_live_screen();
  diag_wdt_feed();
  build_recent_screen();
  build_info_screen();
  diag_wdt_feed();
  return true;
}

void cabin_show_message(const char* title, uint32_t color_hex) {
  if (!scr_msg) return;
  lv_label_set_text(msg_title, title ? title : "");
  lv_obj_set_style_text_color(msg_title, lv_color_hex(color_hex), 0);
  load_screen(scr_msg);
}

void cabin_show_busy(const char* title, uint32_t color_hex) {
  if (!scr_busy) return;
  lv_label_set_text(busy_title, title ? title : "");
  lv_obj_set_style_text_color(busy_title, lv_color_hex(color_hex), 0);
  if (busy_spinner) {
    lv_obj_clear_flag(busy_spinner, LV_OBJ_FLAG_HIDDEN);
  }
  load_screen(scr_busy);
}

void cabin_show_setup(const char* ip_or_null) {
  char line[36];
  if (ip_or_null && ip_or_null[0]) {
    snprintf(line, sizeof(line), "http://%s", ip_or_null);
  } else {
    snprintf(line, sizeof(line), "http://192.168.4.1");
  }
  lv_label_set_text(setup_ip, line);
  load_screen(scr_setup);
}

void cabin_show_page(uint8_t index) {
  if (index == 0) {
    load_screen(scr_live);
  } else if (index == 1) {
    cabin_refresh_recent();
    load_screen(scr_recent);
  } else {
    load_screen(scr_info);
  }
}

uint8_t cabin_page_count(void) { return 3; }

void cabin_refresh_info(int lipo_mv, bool lipo_ok, size_t free_kb) {
  if (info_lipo) {
    char line[40];
    if (lipo_ok) {
      snprintf(line, sizeof(line), "%d.%02d V", lipo_mv / 1000, (lipo_mv % 1000) / 10);
    } else {
      snprintf(line, sizeof(line), "--");
    }
    lv_label_set_text(info_lipo, line);
  }
  if (info_fs) {
    char line[32];
    snprintf(line, sizeof(line), "%u KB free", (unsigned)free_kb);
    lv_label_set_text(info_fs, line);
  }
}

void cabin_refresh_live(const GpsFix* fix, bool logging, uint32_t wifi_total, uint32_t ble_total,
                        uint32_t wifi_session, uint32_t ble_session) {
  if (live_sub) {
    lv_label_set_text(live_sub, logging ? LV_SYMBOL_PLAY " Log ON" : LV_SYMBOL_STOP " Log OFF");
    lv_obj_set_style_text_color(live_sub, lv_color_hex(logging ? kOk : kMuted), 0);
  }
  set_start_stop_labels(logging);

  char c[28];
  if (live_wifi_total) {
    snprintf(c, sizeof(c), "Total: %u", (unsigned)wifi_total);
    lv_label_set_text(live_wifi_total, c);
  }
  if (live_wifi_session) {
    snprintf(c, sizeof(c), "Session: %u", (unsigned)wifi_session);
    lv_label_set_text(live_wifi_session, c);
    lv_obj_set_style_text_color(live_wifi_session, lv_color_hex(logging ? kOk : kMuted), 0);
  }
  if (live_ble_total) {
    snprintf(c, sizeof(c), "Total: %u", (unsigned)ble_total);
    lv_label_set_text(live_ble_total, c);
  }
  if (live_ble_session) {
    snprintf(c, sizeof(c), "Session: %u", (unsigned)ble_session);
    lv_label_set_text(live_ble_session, c);
    lv_obj_set_style_text_color(live_ble_session, lv_color_hex(logging ? kOk : kMuted), 0);
  }

  if (!live_gps || !fix) return;

  char line[48];
  uint32_t color = kMuted;
  switch (fix->state) {
    case kGpsFix:
      snprintf(line, sizeof(line), LV_SYMBOL_GPS " FIX  %u sat", (unsigned)fix->sats);
      color = kOk;
      break;
    case kGpsSearching:
      snprintf(line, sizeof(line), LV_SYMBOL_GPS " searching");
      color = 0xFFD600;
      break;
    case kGpsNoData:
    default:
      snprintf(line, sizeof(line), LV_SYMBOL_WARNING " no GPS");
      color = 0xFF1744;
      break;
  }
  lv_label_set_text(live_gps, line);
  lv_obj_set_style_text_color(live_gps, lv_color_hex(color), 0);

  if (live_coords) {
    if (fix->state == kGpsFix && fix->have_latlon) {
      char coord[56];
      if (is_landscape()) {
        snprintf(coord, sizeof(coord), "%.5f  %.5f", fix->lat_deg, fix->lon_deg);
      } else {
        snprintf(coord, sizeof(coord), "%.5f\n%.5f", fix->lat_deg, fix->lon_deg);
      }
      lv_label_set_text(live_coords, coord);
      lv_obj_set_style_text_color(live_coords, lv_color_hex(kFg), 0);
    } else {
      lv_label_set_text(live_coords, "waiting for fix");
      lv_obj_set_style_text_color(live_coords, lv_color_hex(kMuted), 0);
    }
  }
}

void cabin_refresh_recent(void) {
  const uint8_t n = survey_recent_count();
  const uint32_t gen = survey_recent_gen();
  for (int i = 0; i < recent_line_n; i++) {
    if (!recent_names[i] || !recent_rssis[i]) continue;
    if (i >= n) {
      lv_label_set_text(recent_names[i], "");
      lv_obj_set_style_text_color(recent_names[i], lv_color_hex(kMuted), 0);
      lv_label_set_text(recent_rssis[i], "");
      continue;
    }
    char label[32];
    bool is_ble = false;
    int8_t rssi = -127;
    if (!survey_recent_get((uint8_t)i, label, sizeof(label), &is_ble, &rssi)) {
      lv_label_set_text(recent_names[i], "");
      lv_label_set_text(recent_rssis[i], "");
      continue;
    }
    char left[40];
    snprintf(left, sizeof(left), "%s %s", is_ble ? LV_SYMBOL_BLUETOOTH : LV_SYMBOL_WIFI, label);
    lv_label_set_text(recent_names[i], left);
    lv_obj_set_style_text_color(recent_names[i], lv_color_hex(is_ble ? kPurple : kWifi), 0);

    char right[8];
    snprintf(right, sizeof(right), "%d", (int)rssi);
    lv_label_set_text(recent_rssis[i], right);
    lv_obj_set_style_text_color(recent_rssis[i], lv_color_hex(rssi_color(rssi)), 0);
  }

  if (recent_list && gen != recent_seen_gen) {
    recent_seen_gen = gen;
    lv_obj_scroll_to_y(recent_list, 0, LV_ANIM_ON);
  }
}
