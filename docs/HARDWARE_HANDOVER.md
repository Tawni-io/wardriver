# LILYGO T-Display C5 — hardware handover

Portable BSP notes for a **new firmware project on the same board**. Sourced from VictronDash (`victronBLE`); treat as proven hardware patterns, not product requirements. No Victron Instant Readout / BLE decrypt content here.

**New face workspace:** copy [`TAWNI.md`](TAWNI.md) to the new repo root as `TAWNI.md` (product + board + two-button + SoftAP/OTA + GitHub). This file is the hardware appendix if you only need pins/LCD.

Keep pin map, LCD init, and USB-CDC flags unless you re-validate on hardware.

---

## 1. Board identity

| Item | Value |
| --- | --- |
| Product | [LILYGO T-Display C5](https://www.lilygo.cc/products/t-display-c5) |
| MCU | ESP32-C5 (`esp32c5`) |
| Flash / PSRAM | 16 MB Quad flash, 8 MB Quad PSRAM (`BOARD_HAS_PSRAM`) |
| LCD | ST7789 IPS 1.9″ — native portrait defs 170×320 → cabin **landscape 320×170** |
| Touch | CST816S (I2C `0x15`) — **optional**; many retail units lack CTP |
| PMIC | AXP2602 at I2C `0x62` (chip ID `0x1C`); INT pin defined, unused in app logic |
| PlatformIO board | `Lilygo-T-Display-C5` |
| Board JSON | [`boards/Lilygo-T-Display-C5.json`](../boards/Lilygo-T-Display-C5.json) |
| Pin source of truth | [`include/board_config.h`](../include/board_config.h) + [`include/board_pins.h`](../include/board_pins.h) |

**Stack used here:** Arduino framework via `pioarduino/platform-espressif32` (stable zip), LVGL 9.2, vendored `lib/esp_lcd_st7789` + `lib/CST816S`. Partitions: `default_16MB.csv` (dual OTA slots).

---

## 2. Pin map

| Function | GPIO | Notes |
| --- | --- | --- |
| `BUTTON_PIN` (user / setup) | **0** | Active-low, `INPUT_PULLUP` |
| `BUTTON_BOOT` | **28** | Active-low, `INPUT_PULLUP` |
| LCD CS | **26** | SPI |
| LCD SCK | **7** | SPI |
| LCD MOSI | **9** | SPI (MISO unused `-1`) |
| LCD DC | **8** | |
| LCD RST | **23** | Bit-banged in `display_init` (panel `reset_gpio_num = -1`) |
| LCD backlight | **25** | `LCD_BLK_POWER`, HIGH = on |
| I2C SDA | **2** | Touch + AXP |
| I2C SCL | **3** | |
| TP INT | **27** | Only if `USE_TOUCH_SWIPE` |
| TP RST | **24** | Only if `USE_TOUCH_SWIPE` |
| AXP2602 INT | **10** | Defined; not used in app |

Landscape logical size: `DISPLAY_WIDTH = 320`, `DISPLAY_HEIGHT = 170` (`LCD_HEIGHT` / `LCD_WIDTH` swapped in `board_pins.h`).

USB: on-board USB-C → ESP32-C5 USB-CDC (no separate UART pins in this BSP).

---

## 3. Display (locked path)

Implementation: [`src/display.cpp`](../src/display.cpp), [`src/display.h`](../src/display.h). LVGL flush: [`src/ui/lvgl_port.cpp`](../src/ui/lvgl_port.cpp). Config: [`include/lv_conf.h`](../include/lv_conf.h).

### Init sequence

1. Backlight GPIO25 HIGH first
2. SPI2 @ **40 MHz** (`kSpiClockHz`), DMA auto, transfer queue depth **1**
3. `esp_lcd_new_panel_st7789`, `LCD_RGB_ENDIAN_BGR`, 16 bpp
4. Manual RST on GPIO23: HIGH 25 ms → LOW 25 ms → HIGH 125 ms
5. Landscape apply (`panel_apply_landscape`):
   - Preset **0** (normal): `mirror(true, false)`, `swap_xy(true)`, `invert_color(true)`, gap **`(0, 35)`**
   - Preset **1** (Flip 180): `mirror(false, true)`, same swap / invert / gap
6. Panel wants **byte-swapped RGB565** on the wire (`to_panel_color` / `lv_draw_sw_rgb565_swap` in flush)

### LVGL notes (if you keep LVGL)

- LVGL **9.2**, `-DLV_CONF_INCLUDE_SIMPLE`, `LV_COLOR_DEPTH 16`, `LV_MEM_SIZE (48 * 1024)`
- Display 320×170, `LV_DISPLAY_ROTATION_0`, partial render
- Draw buffer: **20 lines**, `MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA` — **not** full-frame PSRAM DMA (known to corrupt on this SPI panel)
- Blocking flush waits on SPI color-done ISR
- SoftAP path in VictronDash **suspends** the draw buffer to free ~12.8 KB heap — reuse the pattern if WiFi needs RAM

**Flip Display 180:** image only. Enclosure marks the setup button on GPIO0 — **do not remap button roles** when flipped.

---

## 4. Buttons and soft power

The unit has **only two physical buttons**. Cabin UI should stay page-cycle + rare long actions; put complex settings on phone SoftAP (or similar), not nested on-device menus.

### Canonical map (VictronDash today)

| Input | Action | Timing |
| --- | --- | --- |
| GPIO0 short | Next page | release before long |
| GPIO28 short | Previous page | — |
| GPIO0 long **alone** | SoftAP enter / leave | ~2 s (`kLongPressMs`) |
| Both held | Soft power-off → deep sleep | ~3 s (`kSoftOffHoldMs`) |
| GPIO0 after GPIO wake | Confirm stay awake | ~1.5 s (`kWakeConfirmMs`) |

Constants live in [`src/main.cpp`](../src/main.cpp). Dual-hold must suppress the SoftAP long-press (set a “long0 already fired” flag so release does not toggle AP).

New firmware can **redefine** what GPIO0 long means; keep pin roles and timings unless you re-validate collisions with soft-off.

### Soft power / deep sleep

1. Radios down
2. `display_enter_sleep()` — backlight OFF + ST7789 sleep
3. `esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW)` — **GPIO0 only** (wakeable range on this design: IO0–IO6; GPIO28 cannot wake)
4. `esp_deep_sleep_start()`

Wake confirm: if wakeup cause is GPIO, require GPIO0 held ≥ 1.5 s or sleep again. USB/RST cold boot skips confirm.

Board LiPo empty uses the same `soft_power_off()` path: AXP2602 Vbat ≤ 3.20 V while discharging (current < −15 mA) for ~4 s. Do not sleep on USB/charge. Block while SoftAP OTA is active.

---

## 5. Touch (optional CST816S)

| Item | Fact |
| --- | --- |
| Compile flag | `-DUSE_TOUCH_SWIPE=1` in `[env]`; set `0` to compile out |
| Pins | SDA2, SCL3, INT27, RST24 — no jumper |
| Address | `0x15` |
| Driver | Vendored [`lib/CST816S`](../lib/CST816S) (LilyGO fork) |
| App code | [`src/touch.cpp`](../src/touch.cpp), [`src/touch.h`](../src/touch.h) |

### Rules that matter

- Call `Wire.begin(SDA, SCL)` **once** before `CST816S::begin`
- Always pass real `TP_RST` — `rst=-1` becomes GPIO **255** on ESP32-C5 and breaks reset
- Idle: chip often **NACKs at 0x15 while asleep**; that alone is not “missing CTP”
- Presence proof: **`int_edges` rises while touching** — gold test env `bringup_touch` → [`src/touch_stock_main.cpp`](../src/touch_stock_main.cpp)
- LilyGO marks touch optional; Amazon “touch” units can ship without CTP populated
- VictronDash swipe: left/up = next, right/down = previous; invert when Flip On. Buttons remain the fallback

AXP2602 battery read helpers also live in `touch.cpp` (shared I2C bus).

---

## 6. PlatformIO starter

Minimal pattern from [`platformio.ini`](../platformio.ini):

```ini
[platformio]
boards_dir = boards

[env]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = Lilygo-T-Display-C5
framework = arduino
monitor_speed = 115200
monitor_rts = 0
monitor_dtr = 0
upload_speed = 460800
build_flags =
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DARDUINO_USB_MODE=1
  -DLV_CONF_INCLUDE_SIMPLE
  -Iinclude
  -Isrc
  -DUSE_TOUCH_SWIPE=1
```

**Download mode** (if upload fails): hold **BOOT**, tap **RST**, release **BOOT**, then upload again.

**Prereqs:** Python 3.12+, PlatformIO Core, **Git for Windows on PATH** (Espressif platform packages).

---

## 7. Gotchas checklist

1. **USB-CDC reset:** keep `ARDUINO_USB_CDC_ON_BOOT=1` and `monitor_rts` / `monitor_dtr = 0`, or opening the serial monitor resets the board.
2. **`Serial.setTxTimeoutMs(0)`** — avoids CDC TX blocking when no host is attached.
3. **Landscape gap `(0, 35)`** is required for a correct ST7789 window on this panel.
4. **LVGL DMA buffer:** INTERNAL+DMA partial strips only; full-frame PSRAM DMA is known bad here.
5. **Flip 180:** flip the image only; GPIO0 stays the enclosure setup button.
6. **Touch optional:** fail presence on `int_edges`, not idle `probe0x15`.
7. **CST816S RST / Wire:** real RST pin; single `Wire.begin` (see `lib/CST816S` notes).
8. **Deep-sleep wake:** GPIO0 only among the two buttons.
9. **BLE vs WiFi on C5:** continuous NimBLE scan can starve SoftAP — pause one radio before bringing the other up.
10. **OneDrive path:** builds under OneDrive can hit sync locks on `.pio/build` during upload — pause sync or move the project if uploads flake.
11. **Soft-off vs long-press:** dual-hold must consume the long-press so release does not fire the secondary action; block soft-off during OTA if you add OTA.

---

## 8. Files to copy as BSP

Paths relative to this VictronDash repo.

### Copy / adapt

| Path | Why |
| --- | --- |
| `boards/Lilygo-T-Display-C5.json` | Board definition |
| `include/board_config.h`, `include/board_pins.h` | Pins + landscape size |
| `include/lv_conf.h` | LVGL 9.2 board-tuned mem/color (if using LVGL) |
| `src/display.cpp`, `src/display.h` | ST7789 + backlight + flip presets + sleep |
| `src/touch.cpp`, `src/touch.h` | CST816S + AXP helpers (strip product nav if unused) |
| `src/touch_stock_main.cpp` | Hardware CTP acceptance test |
| `src/ui/lvgl_port.cpp`, `src/ui/lvgl_port.h` | Flush / DMA / suspend pattern |
| `lib/esp_lcd_st7789/` | Panel driver |
| `lib/CST816S/` | Touch driver |
| Button / soft-off / wake slice of `src/main.cpp` | Timings + deep sleep |
| Relevant `[env]` flags from `platformio.ini` | USB-CDC, touch flag, `boards_dir` |

### Do not treat as BSP

| Path | Why |
| --- | --- |
| `src/victron/*` | Instant Readout decrypt / models |
| `src/ble_scan.*` | Victron manufacturer-data sniffer |
| `src/ui/dashboard.*`, `src/ui/text.*` | Cabin gauge UI |
| `src/softap/*` | VictronDash setup / OTA / Flip form (reuse SoftAP *pattern* only) |
| `src/config/device_config.*` | NVS device list + keys (extract orient helper only if needed) |
| `include/victron_secrets.h.example` | Secrets template |
| Most of `docs/` except this file and hardware sections of README / DASHBOARD | Product protocol & UX |
| `_restore_single/` | Old snapshot tree |

### Minimal starter set

`board_config.h` + `board_pins.h` + `display.*` + (optional) `touch.*` + `lvgl_port.*` + vendored LCD/touch libs + `Lilygo-T-Display-C5.json` + USB-CDC / `USE_TOUCH_SWIPE` flags + button/sleep constants from `main.cpp`.
