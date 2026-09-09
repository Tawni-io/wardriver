#include "ui/splash.h"

#include <string.h>

#include <lvgl.h>

#include "ui/lvgl_port.h"
#include "ui/splash_logo.h"

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

void splash_show(void) {
  if (!lvgl_port_ready()) {
    return;
  }

  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);

  lv_obj_t* img = lv_image_create(scr);
  lv_image_set_src(img, splash_dsc());
  lv_obj_center(img);

  lv_screen_load(scr);
  lv_obj_invalidate(scr);
  lv_refr_now(nullptr);
  lvgl_port_handler();
}
