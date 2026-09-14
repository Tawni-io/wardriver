#include <Arduino.h>
#include <stdio.h>
#include <esp_ota_ops.h>
#include <esp_sleep.h>

#include "board_pins.h"
#include "config/prefs.h"
#include "diag.h"
#include "display.h"
#include "gps/gps.h"
#include "log/wigle_log.h"
#include "scan/survey.h"
#include "softap/softap.h"
#include "touch.h"
#include "ui/cabin.h"
#include "ui/lvgl_port.h"
#include "ui/splash.h"

#include <lvgl.h>

#ifndef TAWNI_VERSION
#define TAWNI_VERSION "0.0.0"
#endif

namespace {

constexpr uint32_t kSoftOffHoldMs = 3000;
constexpr uint32_t kWakeConfirmMs = 1500;
/** Match Gym Timer / TAWNI SoftAP long-press (~2 s). */
constexpr uint32_t kLongPressMs = 2000;

constexpr int kLipoPresentMinMv = 2500;
constexpr int kLipoEmptyMv = 3200;
constexpr int kIbatThreshMa = 15;
constexpr uint32_t kLipoEmptyHoldMs = 4000;
constexpr uint32_t kLipoEmptyMsgMs = 1500;

uint8_t g_page = 0;
bool g_setup_ui = false;

struct BoardPower {
  bool present;
  bool ok;
  bool on_usb;
  bool voltage_valid;
  int mV;
};

BoardPower g_board_power = {};

void enter_deep_sleep(void) {
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  Serial.println("Soft power → deep sleep (wake: hold GPIO0)");
  Serial.flush();
  delay(50);
  esp_deep_sleep_start();
}

void pump_ui(uint32_t ms) {
  const uint32_t until = millis() + ms;
  while (millis() < until) {
    lvgl_port_handler();
    delay(10);
  }
}

void abort_soft_off_gps_awake(void) {
  gps_wake_run();
  survey_resume();
  cabin_show_message("GPS SLEEP FAILED", 0xFF1744);
  pump_ui(1500);
  cabin_show_page(g_page);
  Serial.println("Soft power aborted — back to cabin");
}

void soft_power_off(bool require_gps_standby) {
  Serial.println("Soft power off");
  if (softap_active()) {
    softap_stop();
    softap_clear_stop_request();
    g_setup_ui = false;
    if (!lvgl_port_resume_draw_buf()) {
      Serial.println("LVGL resume failed — restarting");
      delay(200);
      ESP.restart();
    }
  }
  survey_pause();
  cabin_show_message("Shutting Down...", 0x00E676);
  pump_ui(80);
  if (!gps_enter_standby()) {
    Serial.println("GPS standby not confirmed");
    if (require_gps_standby) {
      abort_soft_off_gps_awake();
      return;
    }
    Serial.println("GPS still awake — sleeping anyway");
  }
  display_enter_sleep();
  delay(100);
  enter_deep_sleep();
}

void poll_board_power(void) {
  static uint32_t last_read_ms = 0;
  const uint32_t now = millis();
  if (last_read_ms != 0 && (now - last_read_ms) < 2000u) {
    return;
  }
  last_read_ms = now;

  g_board_power.present = board_power_present();
  if (!g_board_power.present) {
    g_board_power.ok = false;
    g_board_power.on_usb = true;
    g_board_power.voltage_valid = false;
    g_board_power.mV = 0;
    return;
  }

  int mV = 0;
  int i_mA = 0;
  g_board_power.ok = board_power_read(&mV, nullptr, &i_mA);
  if (!g_board_power.ok) {
    return;
  }
  g_board_power.mV = mV;
  g_board_power.voltage_valid = mV >= kLipoPresentMinMv;
  g_board_power.on_usb = (i_mA >= -kIbatThreshMa);
}

void maybe_lipo_empty_sleep(void) {
  static uint32_t below_since_ms = 0;
  if (softap_ota_active()) {
    below_since_ms = 0;
    return;
  }
  poll_board_power();
  const bool empty = g_board_power.ok && !g_board_power.on_usb && g_board_power.voltage_valid &&
                     g_board_power.mV <= kLipoEmptyMv;
  if (!empty) {
    below_since_ms = 0;
    return;
  }
  const uint32_t now = millis();
  if (below_since_ms == 0) {
    below_since_ms = now;
    Serial.printf("LiPo low %d mV — soft-off if still discharging\n", g_board_power.mV);
  }
  if ((now - below_since_ms) < kLipoEmptyHoldMs) {
    return;
  }

  Serial.printf("LiPo empty %d mV → soft-off\n", g_board_power.mV);
  if (!softap_active()) {
    cabin_show_message("BATTERY EMPTY", 0xFF1744);
    const uint32_t until = millis() + kLipoEmptyMsgMs;
    while (millis() < until) {
      lvgl_port_handler();
      delay(10);
    }
  }
  soft_power_off(false);
}

bool woke_from_gpio(void) {
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
}

void abort_gpio_wake_if_released(bool panel_on) {
  if (!woke_from_gpio()) {
    return;
  }
  if (digitalRead(BUTTON_PIN) != LOW) {
    Serial.println("Wake confirm released → sleep again");
    gps_enter_standby();
    if (panel_on) {
      display_enter_sleep();
    }
    enter_deep_sleep();
  }
}

void enter_softap(void) {
  if (softap_active()) return;

  survey_pause();
  Serial.printf("Entering SoftAP setup (free heap %u)\n", (unsigned)ESP.getFreeHeap());
  if (!softap_start()) {
    cabin_show_message("WIFI FAILED", 0xFF1744);
    g_setup_ui = false;
    survey_resume();
    return;
  }
  cabin_show_setup(softap_ip());
  g_setup_ui = true;
  for (int i = 0; i < 10; i++) {
    lvgl_port_handler();
    delay(5);
  }
  lvgl_port_suspend_draw_buf();
}

void leave_softap(void) {
  if (!softap_active()) return;

  Serial.println("SoftAP leave");
  gps_wake_run();
  softap_stop();
  softap_clear_stop_request();

  if (!lvgl_port_resume_draw_buf()) {
    Serial.println("LVGL resume failed — restarting to reclaim heap");
    delay(200);
    ESP.restart();
  }

  g_setup_ui = false;
  cabin_show_message("LEAVING SETUP", 0x00E676);

  const uint32_t settle_until = millis() + 800;
  while (millis() < settle_until) {
    lvgl_port_handler();
    delay(10);
  }

  survey_resume();
  cabin_show_page(g_page);
  Serial.println("SoftAP leave → cabin UI armed");
}

void page_next(void) {
  const uint8_t n = cabin_page_count();
  if (n <= 1) return;
  g_page = (uint8_t)((g_page + 1) % n);
  cabin_show_page(g_page);
  Serial.printf("Page %u/%u\n", (unsigned)(g_page + 1), (unsigned)n);
}

void page_prev(void) {
  const uint8_t n = cabin_page_count();
  if (n <= 1) return;
  g_page = (uint8_t)((g_page + n - 1) % n);
  cabin_show_page(g_page);
  Serial.printf("Page %u/%u\n", (unsigned)(g_page + 1), (unsigned)n);
}

void toggle_logging(void) {
  const bool on = !wigle_logging();
  Serial.printf("Cabin logging → %s\n", on ? "ON" : "OFF");

  if (on) {
    cabin_show_busy(LV_SYMBOL_PLAY " Starting...", 0x00E676);
  } else {
    cabin_show_busy(LV_SYMBOL_STOP " Stopping...", 0xFFD600);
  }
  // Paint busy screen before radio work blocks
  for (int i = 0; i < 8; i++) {
    lvgl_port_handler();
    delay(5);
  }

  wigle_set_logging(on);
  survey_apply_logging(on);

  const uint32_t until = millis() + (on ? 1200u : 900u);
  while (millis() < until) {
    lvgl_port_handler();
    gps_loop();
    diag_wdt_feed();
    delay(10);
  }

  cabin_show_page(g_page);
  GpsFix fix = {};
  gps_get(&fix);
  cabin_refresh_live(&fix, wigle_logging(), wigle_wifi_rows(), wigle_ble_rows(),
                     survey_wifi_seen(), survey_ble_seen());
}

void poll_buttons(void) {
  static bool was0 = false;
  static bool was28 = false;
  static uint32_t down0_ms = 0;
  static uint32_t down28_ms = 0;
  static uint32_t both_ms = 0;
  static bool long0_fired = false;
  static bool long28_fired = false;
  static bool soft_off_fired = false;

  const bool down0 = digitalRead(BUTTON_PIN) == LOW;
  const bool down28 = digitalRead(BUTTON_BOOT) == LOW;
  const uint32_t now = millis();

  // Both held → soft-off (suppresses bottom SoftAP long-press)
  if (down0 && down28) {
    if (both_ms == 0) {
      both_ms = now;
      soft_off_fired = false;
    }
    if (!soft_off_fired && (now - both_ms) >= kSoftOffHoldMs) {
      soft_off_fired = true;
      long0_fired = true;
      long28_fired = true;
      if (softap_ota_active()) {
        Serial.println("Soft power ignored — OTA active");
      } else {
        soft_power_off(true);
      }
    }
  } else {
    both_ms = 0;
  }

  if (down0 && !was0) {
    down0_ms = now;
    long0_fired = false;
  }
  if (down28 && !was28) {
    down28_ms = now;
    long28_fired = false;
  }

  // Bottom (GPIO0 / marked setup): long alone → SoftAP (Gym README / TAWNI ~2 s)
  if (down0 && !down28 && !long0_fired && (now - down0_ms) >= kLongPressMs) {
    long0_fired = true;
    if (softap_active()) {
      Serial.println("GPIO0 long → leave SoftAP");
      leave_softap();
    } else {
      Serial.println("GPIO0 long → enter SoftAP");
      enter_softap();
    }
  }

  // Top (GPIO28): long alone → previous page (Gym: top long = mode switch)
  if (down28 && !down0 && !long28_fired && !softap_active() &&
      (now - down28_ms) >= kLongPressMs) {
    long28_fired = true;
    page_prev();
  }

  // Bottom short → start/stop logging (Gym: bottom short = start/stop)
  if (!down0 && was0 && !long0_fired) {
    if (!softap_active()) {
      toggle_logging();
    }
  }

  // Top short → next page (Gym: top short = secondary / +30 / zero)
  if (!down28 && was28 && !long28_fired) {
    if (!softap_active()) {
      page_next();
    }
  }

  was0 = down0;
  was28 = down28;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(300);
  if (Serial) {
    Serial.println();
    Serial.printf("Tawni Wardriver v%s — Rufous Phase D (survey)\n", TAWNI_VERSION);
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUTTON_BOOT, INPUT_PULLUP);

  abort_gpio_wake_if_released(false);

  if (!display_init()) {
    while (true) delay(1000);
  }

  prefs_begin();
  {
    const uint8_t layout = prefs_get_orient();
    display_apply_layout(layout);
    Serial.printf("Boot layout %s\n",
                  layout == kOrientLandscape ? "landscape" : "portrait");
  }

  if (!lvgl_port_init()) {
    Serial.println("LVGL init failed — halt");
    while (true) delay(1000);
  }

  diag_begin();

  abort_gpio_wake_if_released(true);

  gps_begin();
  gps_set_mirror_raw(false);
  const bool from_gpio = woke_from_gpio();
  if (!from_gpio) {
    gps_wake_run();
  }

  splash_show();
  Serial.printf("Splash (wake cause %d) WUP=%s\n", (int)esp_sleep_get_wakeup_cause(),
                gps_wakeup_is_run() ? "H" : "L");
  if (from_gpio) {
    splash_set_status("GPS", 0x8B949E);
  }
  {
    const uint32_t start = millis();
    while ((millis() - start) < kWakeConfirmMs) {
      gps_loop();
      if (from_gpio && ((millis() - start) >= 600u)) {
        GpsFix probe = {};
        gps_get(&probe);
        const bool wup_run = gps_wakeup_is_run();
        const bool quiet = (probe.state == kGpsNoData);
        if (!wup_run && quiet) {
          splash_set_status("GPS", 0x00E676);
        } else if (wup_run || probe.sentences > 0) {
          splash_set_status("GPS", 0xFF1744);
        }
      }
      lvgl_port_handler();
      delay(10);
    }
  }

  if (from_gpio) {
    gps_wake_run();
  }

  if (!cabin_init()) {
    Serial.println("Cabin init failed — halt");
    while (true) delay(1000);
  }

#if USE_TOUCH_SWIPE
  touch_init();
#else
  // Still need Wire + AXP for LiPo soft-off when touch is compiled out.
  // touch_init is a no-op stub in that build — skip LiPo auto-off path then.
#endif

  wigle_begin();
  wigle_set_logging(false);  // never auto-start after boot
  survey_begin();
  survey_resume();

  {
    const esp_err_t ota_ok = esp_ota_mark_app_valid_cancel_rollback();
    if (ota_ok != ESP_OK && Serial) {
      Serial.printf("OTA mark valid: %s\n", esp_err_to_name(ota_ok));
    }
  }

  g_page = 0;
  cabin_show_page(g_page);
  splash_discard();
  Serial.println("Cabin ready — logging OFF until bottom short-press");
}

void loop() {
  static uint32_t last_info_ms = 0;
  static uint32_t last_gps_ui_ms = 0;
  static uint32_t last_recent_ui_ms = 0;

  diag_wdt_feed();
  gps_loop();
  poll_buttons();
  maybe_lipo_empty_sleep();

#if USE_TOUCH_SWIPE
  {
    const TouchNav nav = touch_poll();
    if (!softap_active()) {
      if (nav == kTouchNavNext) {
        page_next();
      } else if (nav == kTouchNavPrev) {
        page_prev();
      }
    }
  }
#endif

  if (softap_active()) {
    softap_loop();
    if (softap_stop_requested()) {
      Serial.println("SoftAP /stop requested");
      leave_softap();
    }
    delay(2);
    return;
  }

  survey_loop();
  lvgl_port_handler();

  const uint32_t now = millis();
  if ((now - last_gps_ui_ms) >= 500u) {
    last_gps_ui_ms = now;
    GpsFix fix = {};
    gps_get(&fix);
    cabin_refresh_live(&fix, wigle_logging(), wigle_wifi_rows(), wigle_ble_rows(),
                     survey_wifi_seen(), survey_ble_seen());
  }
  if (g_page == 1 && (now - last_recent_ui_ms) >= 200u) {
    last_recent_ui_ms = now;
    cabin_refresh_recent();
  }
  if ((now - last_info_ms) >= 2000u) {
    last_info_ms = now;
    poll_board_power();
    cabin_refresh_info(g_board_power.mV, g_board_power.ok && g_board_power.voltage_valid,
                       wigle_free_bytes() / 1024);
  }
}
