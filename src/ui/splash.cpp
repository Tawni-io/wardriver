#include "ui/splash.h"

#include <string.h>

#include <lvgl.h>

#include "ui/lvgl_port.h"
#include "ui/splash_logo.h"
#include "display.h"

namespace {

const lv_image_dsc_t* splash_dsc(void) {
  static lv_image_dsc_t dsc;
  static bool ready = false;
  if (!ready) {
    memset(&dsc, 0, sizeof(dsc));
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    dsc.header.w = (uint32_t)kSplashLogoW;
    dsc.header.h = (uint32_t)kSplashLogoH;
    dsc.header.stride = (uint32_t)kSplashLogoW * 2u;
    dsc.data_size = (uint32_t)kSplashLogoW * (uint32_t)kSplashLogoH * 2u;
    dsc.data = (const uint8_t*)kSplashLogo;
    ready = true;
  }
  return &dsc;
}

}  // namespace

static lv_obj_t* g_splash_scr = nullptr;
static lv_obj_t* g_splash_status = nullptr;

void splash_show(void) {
  if (!lvgl_port_ready()) {
    return;
  }

  lv_obj_t* scr = lv_obj_create(nullptr);
  g_splash_scr = scr;
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);

  lv_obj_t* img = lv_image_create(scr);
  lv_image_set_src(img, splash_dsc());
  const int max_w = display_width() - 10;
  if (kSplashLogoW > max_w) {
    lv_image_set_scale(img, (int32_t)((256 * max_w) / kSplashLogoW));
  }
  lv_obj_align(img, LV_ALIGN_CENTER, 0, -12);

  g_splash_status = lv_label_create(scr);
  lv_obj_set_style_text_font(g_splash_status, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(g_splash_status, lv_color_hex(0x8B949E), 0);
  lv_label_set_text(g_splash_status, "");
  lv_obj_align(g_splash_status, LV_ALIGN_BOTTOM_MID, 0, -10);

  lv_screen_load(scr);
  lv_obj_invalidate(scr);
  lv_refr_now(nullptr);
  lvgl_port_handler();
}

void splash_set_status(const char* text, uint32_t color_hex) {
  if (!g_splash_status) return;
  lv_label_set_text(g_splash_status, text ? text : "");
  lv_obj_set_style_text_color(g_splash_status, lv_color_hex(color_hex), 0);
  lv_obj_invalidate(g_splash_status);
  lvgl_port_handler();
}

void splash_discard(void) {
  if (!g_splash_scr) return;
  if (lv_screen_active() != g_splash_scr) {
    lv_obj_delete(g_splash_scr);
  }
  g_splash_scr = nullptr;
  g_splash_status = nullptr;
}
