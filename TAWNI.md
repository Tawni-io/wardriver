# Tawni — new face workspace briefing

**Copy this file into every new face repo as `TAWNI.md` (repo root).** Agents: read this first. It is the product, the board, and the firmware contract. Fill in §0, then implement. Do not invent a second hardware map.

Proven BSP: VictronDash (`victronBLE`). That repo is the first **face**, not the whole product. Do **not** copy Victron Instant Readout / BLE decrypt into a new face.

---

## 0. This face (fill in before coding)

| Field | Value |
| --- | --- |
| Face name | Wardriver |
| One-sentence job | Wi-Fi + BLE survey; log to flash, export from SoftAP |
| GitHub repo | _TBD — add remote when Tawni GitHub exists_ |
| SoftAP SSID | `TawniWardriver` |
| Version define | `-DTAWNI_VERSION=\"0.0.0\"` |
| Release `.bin` name | `wardriver-t-display-c5-vX.Y.Z.bin` |

Cabin = page cycle + rare long actions. Settings = this SoftAP SSID on a phone. If a setting cannot be reached with two buttons, it belongs on SoftAP.

---

## 1. What Tawni is

**[Tawni.io](https://tawni.io)** — same printed box, different firmware **face**.

- Pocket dashboard: 1.9″ IPS, 2.4 / 5 GHz Wi‑Fi, BLE 5, rugged enclosure, ~1100 mAh LiPo.
- Sold as hardware (pre-flashed or browser-flash). Firmware is swappable.
- Brand splash: TAWNI mark on black at boot (`splash_show()` after LVGL init). Keep it unless the face has a strong reason not to.

**Faces** are separate Cursor/git projects on the **same hardware**. Shared: case, two buttons, LCD path, SoftAP/OTA pattern, deep sleep. Different: cabin UI, radios job, SoftAP form fields.

**This box cannot do:** GPS, microSD, LoRa, IMU/accelerometer, NFC. No analog 5.8 GHz FPV. Logs dump over SoftAP/USB, not an SD card. Hourglass-style flip detect needs an add-on I2C accel (not stock).

---

## 2. Board (locked)

| Item | Value |
| --- | --- |
| Product | [LILYGO T-Display C5](https://www.lilygo.cc/products/t-display-c5) |
| MCU | ESP32-C5 (`esp32c5`), 240 MHz RISC-V |
| Flash / PSRAM | 16 MB Quad flash, 8 MB Quad PSRAM (`BOARD_HAS_PSRAM`) |
| LCD | ST7789 IPS 1.9″ — native 170×320 portrait defs → cabin **landscape 320×170** |
| Touch | CST816S I2C `0x15` — **optional**; many retail units have no CTP |
| PMIC | AXP2602 I2C `0x62` (chip ID `0x1C`) |
| USB | USB-C → ESP32-C5 USB-CDC (no extra UART in this BSP) |
| Expansion | I2C Qwiic on GPIO2/3 (shared with touch + AXP); headers |
| PlatformIO board | `Lilygo-T-Display-C5` + `boards/Lilygo-T-Display-C5.json` |
| Partitions | `default_16MB.csv` (dual OTA `app0` / `app1`, ~6.25 MB each) |
| Stack | Arduino via `pioarduino/platform-espressif32` **stable zip**, LVGL **9.2** |

Pin source of truth once copied: `include/board_config.h` + `include/board_pins.h`.

### Pin map

| Function | GPIO | Notes |
| --- | --- | --- |
| `BUTTON_PIN` (user / setup) | **0** | Active-low, `INPUT_PULLUP`. Enclosure **marks this button**. |
| `BUTTON_BOOT` | **28** | Active-low, `INPUT_PULLUP`. Cannot wake from deep sleep. |
| LCD CS / SCK / MOSI | 26 / 7 / 9 | SPI; MISO unused (`-1`) |
| LCD DC / RST / BL | 8 / 23 / 25 | RST bit-banged; BL HIGH = on |
| I2C SDA / SCL | **2 / 3** | Touch + AXP + any Qwiic sensor |
| TP INT / RST | 27 / 24 | Only if `USE_TOUCH_SWIPE` |
| AXP2602 INT | 10 | Defined; unused in VictronDash app logic |

Landscape: `DISPLAY_WIDTH = 320`, `DISPLAY_HEIGHT = 170` (`LCD_HEIGHT` / `LCD_WIDTH` swapped).

---

## 3. Two-button UX (non-negotiable)

There is **no** on-device settings menu. Touch swipe is optional. Buttons stay the cabin input.

| Input | Default action | Timing |
| --- | --- | --- |
| GPIO0 short | Next page (or face-specific primary short) | Release before long |
| GPIO28 short | Previous page (or secondary short) | — |
| GPIO0 long **alone** | SoftAP enter / leave | ~2 s (`kLongPressMs`) |
| Both held | Soft power-off → deep sleep | ~3 s (`kSoftOffHoldMs`) |
| GPIO0 after GPIO wake | Confirm stay awake | ~1.5 s (`kWakeConfirmMs`) |

**Rules**

- No nested cabin menus that need select / back / adjust with two keys.
- Do **not** remap GPIO0 vs GPIO28 when **Flip Display 180** is on — the enclosure marks setup.
- Dual-hold must suppress the SoftAP long-press (flag so GPIO0 release does not toggle AP).
- Soft-off blocked during OTA. GPIO0 alone still leaves SoftAP while the AP is up.
- Prefer short-press for nav; do not add new chords that collide with 2 s / 3 s.
- Swipe (if CTP present) may mirror next/previous. Invert swipe when Flip is On. Buttons always work.

Copy `.cursor/rules/two-button-nav.mdc` from VictronDash into the new repo and replace the SoftAP SSID name with this face’s SSID.

---

## 4. Display (locked path — do not “simplify”)

1. Backlight GPIO25 HIGH first.
2. SPI2 @ **40 MHz**, DMA auto, transfer queue depth **1**.
3. `esp_lcd_new_panel_st7789`, `LCD_RGB_ENDIAN_BGR`, 16 bpp.
4. Manual RST GPIO23: HIGH 25 ms → LOW 25 ms → HIGH 125 ms (`reset_gpio_num = -1` on the panel).
5. Landscape (`panel_apply_landscape`):
   - Preset **0**: `mirror(true, false)`, `swap_xy(true)`, `invert_color(true)`, gap **`(0, 35)`**
   - Preset **1** (Flip 180): `mirror(false, true)`, same swap / invert / gap
6. Wire wants **byte-swapped RGB565** (`to_panel_color` / `lv_draw_sw_rgb565_swap` in flush).

### LVGL

- LVGL 9.2, `-DLV_CONF_INCLUDE_SIMPLE`, `LV_COLOR_DEPTH 16`, `LV_MEM_SIZE (48 * 1024)`
- 320×170, `LV_DISPLAY_ROTATION_0`, **partial** render
- Draw buffer: **20 lines**, `MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`
- **Do not** full-frame PSRAM DMA — known to corrupt this SPI panel
- Blocking flush waits on SPI color-done ISR
- SoftAP: **suspend** the LVGL draw buffer to free ~12.8 KB heap if Wi‑Fi is tight

Cabin UI: high contrast, large type, one job per page, honest empty/stale states. Landscape only — no auto-rotate, no portrait.

---

## 5. Power, I2C, radios

### Soft-off / deep sleep

1. Radios down.
2. `display_enter_sleep()` — backlight off + ST7789 sleep.
3. `esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW)` — **GPIO0 only** (wakeable range IO0–IO6).
4. `esp_deep_sleep_start()`.

Wake confirm: GPIO wakeup → hold GPIO0 ≥ 1.5 s or sleep again. USB/RST cold boot skips confirm.

Empty LiPo: AXP2602 Vbat ≤ **3.20 V** while discharging (current < −15 mA) for ~4 s → same soft-off. Do **not** sleep on USB/charge. Block soft-off during OTA.

### Touch (CST816S)

- `-DUSE_TOUCH_SWIPE=1` to include; `0` to compile out.
- `Wire.begin(SDA, SCL)` **once** before `CST816S::begin`.
- Always pass real `TP_RST` (GPIO 24). `rst=-1` becomes GPIO **255** on ESP32-C5 and breaks reset.
- Idle NACK at `0x15` is normal (chip asleep). Presence = **`int_edges` rises while touching**.
- Gold test in VictronDash: `pio run -e bringup_touch -t upload`.

AXP battery helpers share this I2C bus. Extra sensors (e.g. LIS3DH hourglass) also use GPIO2/3 — pick an address that is not `0x15` (touch) or `0x62` (AXP). LIS3DH `0x19`/`0x18` is safe; BMA400 alt `0x15` is not if CTP is fitted.

### BLE vs Wi‑Fi

ESP32-C5: **pause NimBLE scan before SoftAP**, and vice versa. Continuous BLE scan starves the AP.

---

## 6. SoftAP + OTA (reuse the pattern)

Phone is the settings surface. One page. Not a cabin wizard.

**Feel**

1. First boot / no config → SoftAP already on; cabin shows **SETUP MODE** + this face’s SSID.
2. Join SSID → `http://192.168.4.1` → one page (status, Flip 180, face settings, Firmware, Stop hotspot).
3. Later: GPIO0 long toggles the AP. Tap Stop (or long-press) to return to cabin pages.

**Always on that page**

- Version string
- **Flip Display 180** (Off/On) — image only; buttons do not swap
- Firmware upload (app `.bin` only) + Factory reset (wipe NVS → SETUP MODE)
- Stop hotspot

MVP hotspot is **open** (no password) unless the face has a reason to PIN it.

**OTA**

- Dual slots. SoftAP writes the **inactive** app slot.
- After healthy boot: `esp_ota_mark_app_valid_cancel_rollback`.
- Ship `.pio/build/<env>/firmware.bin` — **not** a merged bootloader+partitions image.
- Field builds: NVS **survives** OTA (`WIPE_CONFIG_ON_NEW_FW=0`). Factory reset is how users wipe.
- Stay powered during upload. Prefer a normal browser tab, not a flaky captive-portal sheet.
- No ArduinoOTA, no always-on van Wi‑Fi, no auto-download from GitHub in MVP.

USB-C = first flash and recovery only.

---

## 7. Toolchain and first flash

**Prereqs:** Python 3.12+, PlatformIO Core, **Git for Windows on PATH**, USB-C cable.

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

```bash
pio run -e <env>
pio run -e <env> -t upload
pio device monitor -b 115200
```

**Download mode** if upload fails: hold **BOOT**, tap **RST**, release **BOOT**, upload again.

**Windows UTF-8 (required before upload):** esptool’s progress bar can hang the COM port on cp1252:

```powershell
chcp 65001
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"
pio run -e <env> -t upload
```

If it already hung: unplug USB / kill stuck `esptool`, then retry. Do not start a second upload.

`Serial.setTxTimeoutMs(0)` so CDC does not block with no host attached.

---

## 8. Files to copy from VictronDash

Paths relative to the VictronDash (`victronBLE`) repo.

### Copy / adapt (BSP)

| Path | Why |
| --- | --- |
| `boards/Lilygo-T-Display-C5.json` | Board definition |
| `include/board_config.h`, `include/board_pins.h` | Pins + landscape size |
| `include/lv_conf.h` | LVGL 9.2 mem/color |
| `src/display.cpp`, `src/display.h` | ST7789 + backlight + flip + sleep |
| `src/touch.cpp`, `src/touch.h` | CST816S + AXP helpers (strip Victron nav) |
| `src/ui/lvgl_port.cpp`, `src/ui/lvgl_port.h` | Flush / DMA / suspend |
| `src/ui/splash.cpp`, `src/ui/splash.h`, `src/ui/splash_logo.h` | TAWNI boot mark |
| `tools/gen_splash_logo.py` | Regen splash from PNG if needed |
| `lib/esp_lcd_st7789/` | Panel driver |
| `lib/CST816S/` | Touch driver |
| Button / soft-off / wake slice of `src/main.cpp` | Timings + deep sleep |
| SoftAP **skeleton** in `src/softap/` | Pattern only — new SSID, new form |
| USB-CDC / touch flags from `platformio.ini` | See §7 |
| `.cursor/rules/two-button-nav.mdc` | Agent UX rule |
| This file | `TAWNI.md` at new repo root |

### Do not copy

| Path | Why |
| --- | --- |
| `src/victron/*` | Instant Readout decrypt |
| `src/ble_scan.*` | Victron manufacturer-data sniffer |
| `src/ui/dashboard.*`, `src/ui/text.*` | Victron cabin gauges |
| `src/config/device_config.*` | Victron NVS device list/keys (steal Flip/NVS helpers only if useful) |
| `include/victron_secrets.h*` | Secrets |
| Victron product docs (`PRODUCT.md`, `INSTANT_READOUT.md`, …) | Wrong face |
| `_restore_single/`, `pics/`, `site/checkout/.env` | Snapshots / secrets |

### Suggested new-repo layout

```
TAWNI.md
README.md
platformio.ini
.gitignore          # .pio/, *.elf, secrets; ship bins via GitHub Releases
boards/
include/
lib/
src/main.cpp
src/display.*
src/touch.*
src/softap/
src/ui/
.cursor/rules/two-button-nav.mdc
```

Bring-up order: USB CDC serial → LCD landscape + splash → buttons + soft-off → SoftAP + Flip + OTA → then this face’s job.

---

## 9. GitHub — ready to push

Do **not** commit until the human asks. When they do:

1. Root `README.md`: face name, job, board, `pio run` / upload, SoftAP SSID, button map.
2. `.gitignore`: `.pio/`, `*.elf`, `*.map`, secrets. Prefer Releases for `.bin`, not git.
3. Version: one `-DTAWNI_VERSION=\"X.Y.Z\"` (or face-specific define) in `platformio.ini` **and** matching `#ifndef` fallbacks. Semver.
4. `pio run` succeeds; app `.bin` fits an OTA slot (~6.25 MB).
5. Smoke: USB flash → splash + cabin → GPIO0 long → join SSID → Flip works → SoftAP OTA of the same `.bin` → version bumps → NVS still there.
6. First push: `main` (or `master`), then tag `vX.Y.Z` and a GitHub Release attaching `{face}-t-display-c5-vX.Y.Z.bin`.
7. No keys, no `.env`, no Victron MAC/key dumps.

USB first-flash / recovery; SoftAP for field updates. Device does not phone home.

---

## 10. Gotchas (read before “fixing” the BSP)

1. USB-CDC: `ARDUINO_USB_CDC_ON_BOOT=1` and `monitor_rts` / `monitor_dtr = 0`, or opening serial **resets** the board.
2. Landscape gap `(0, 35)` is required.
3. LVGL buffer = internal DMA strips, not PSRAM full frames.
4. Flip 180 = image only; GPIO0 stays the marked setup button.
5. Touch presence = `int_edges`, not idle `probe0x15`.
6. One `Wire.begin`; real CST816S RST pin.
7. Deep-sleep wake = GPIO0 only.
8. BLE scan vs SoftAP: one radio at a time.
9. OneDrive paths can lock `.pio/build` during upload — pause sync or move the project.
10. Dual-hold consumes GPIO0 long-press.
11. Windows upload: UTF-8 (`chcp 65001` + `PYTHONUTF8`) or esptool hangs the port.

---

## 11. Agent contract

- Fill §0. Keep pin map, LCD init, USB-CDC flags unless re-validated on hardware.
- Answer for every feature: *how does the user reach this with two buttons?* If they cannot, SoftAP or defer.
- Do not port Victron decrypt, do not add GPS/SD/LoRa pretence, do not nest cabin menus.
- Prefer copying proven display/touch/sleep/SoftAP code over rewriting it.
- Ask before `git commit` / `git push` unless the human already asked.
