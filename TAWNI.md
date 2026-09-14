# Tawni — Wardriver Firmware workspace briefing

> **Shipping law (wins on conflict):** [TAWNI_RULES.md](../../TAWNI_RULES.md) at the `Tawni.io` workspace root.
> This file is background only (board / product / UX detail). If anything here disagrees with the law, **follow the law** and ask the orchestrator.

**Agents: read this first.** Product, board, Rufous delta, and firmware contract for **this** Firmware. Shared BSP rules still apply; do not invent a second pin map for the C5 core.

Proven BSP: Victron Instant Readout (`Firmware/Victron Instant Readout`). That repo is the first **firmware**, not the whole product. Do **not** copy Victron Instant Readout / BLE decrypt into this Firmware.

---

## 0. This firmware

| Field | Value |
| --- | --- |
| Firmware name | Wardriver |
| Hardware SKU | **Rufous only** (not base Tawni) |
| One-sentence job | Passive Wi‑Fi + BLE survey with GPS; log Wigle CSV; export from SoftAP |
| GitHub repo | `Tawni-io/wardriver` (org exists; recreate/publish from this folder when orchestrator says - do not use legacy folders) |
| SoftAP SSID | `TawniWardriver` |
| Version define | `-DTAWNI_VERSION=\"0.3.4\"` |
| Release `.bin` name | `wardriver-rufous-c5-vX.Y.Z.bin` |

Cabin = page cycle + rare long actions. Settings / export = SoftAP on a phone. If a setting cannot be reached with two buttons, it belongs on SoftAP.

**Scope (MVP):** passive scan / log / export only. No deauth, Evil Portal, or other active RF tools.

---

## 1. What Tawni / Rufous is

**[Tawni.io](https://tawni.io)** — pocket dashboards on LILYGO T-Display C5 in a custom enclosure. Firmware is swappable (**firmwares**).

| SKU | Contents | Fits this Firmware? |
| --- | --- | --- |
| **Tawni** | C5 + LiPo + 2 buttons | **No** — no GPS |
| **Rufous** | Tawni + **GPS** + **external Wi‑Fi/BT antenna** | **Yes** — required |

**Firmwares** are separate git projects. Shared: case language, two buttons, LCD path, SoftAP/OTA pattern, deep sleep. Different: cabin UI, radios job, SoftAP fields, and (for Rufous firmwares) GPS.

Brand splash: TAWNI mark on black at boot (`splash_show()` after LVGL init). Keep it unless the firmware has a strong reason not to.

**Base Tawni cannot do:** GPS, microSD, LoRa, IMU, NFC. **Rufous adds GPS** (and external antenna). Still no microSD — logs live in flash and dump over SoftAP/USB.

---

## 2. Board (locked C5 core)

| Item | Value |
| --- | --- |
| Product | [LILYGO T-Display C5](https://www.lilygo.cc/products/t-display-c5) |
| MCU | ESP32-C5 (`esp32c5`), 240 MHz RISC-V |
| Flash / PSRAM | 16 MB Quad flash, 8 MB Quad PSRAM (`BOARD_HAS_PSRAM`) |
| LCD | ST7789 IPS 1.9″ — native **170×320 portrait** cabin (Flip 180 via SoftAP) |
| Touch | CST816S I2C `0x15` — **optional**; many retail units have no CTP |
| PMIC | AXP2602 I2C `0x62` (chip ID `0x1C`) |
| USB | USB-C → ESP32-C5 USB-CDC (no extra UART in this BSP) |
| Expansion | I2C Qwiic on GPIO2/3 (shared with touch + AXP); headers; **UART Qwiic / header for GPS** |
| PlatformIO board | `Lilygo-T-Display-C5` + `boards/Lilygo-T-Display-C5.json` |
| Partitions | `default_16MB.csv` (dual OTA `app0` / `app1`, ~6.25 MB each) |
| Stack | Arduino via `pioarduino/platform-espressif32` **stable zip**, LVGL **9.2** |

Pin source of truth: `include/board_config.h` + `include/board_pins.h` (+ GPS defines when locked).

### Pin map (C5 core)

| Function | GPIO | Notes |
| --- | --- | --- |
| `BUTTON_PIN` (user / setup) | **0** | Active-low, `INPUT_PULLUP`. Enclosure **marks this button**. |
| `BUTTON_BOOT` | **28** | Active-low, `INPUT_PULLUP`. Cannot wake from deep sleep. |
| LCD CS / SCK / MOSI | 26 / 7 / 9 | SPI; MISO unused (`-1`) |
| LCD DC / RST / BL | 8 / 23 / 25 | RST bit-banged; BL HIGH = on |
| I2C SDA / SCL | **2 / 3** | Touch + AXP + any Qwiic sensor |
| TP INT / RST | 27 / 24 | Only if `USE_TOUCH_SWIPE` |
| AXP2602 INT | 10 | Defined; unused in VictronDash app logic |

Portrait: `DISPLAY_WIDTH = 170`, `DISPLAY_HEIGHT = 320` (`LCD_WIDTH` / `LCD_HEIGHT`).

### Rufous delta — GPS (this Firmware)

First Rufous hardware bring-up (built 2026-09-09). Wiring on right header silk **GND / 3V3 / TXD / RXD**, UART crossed:

| XIAO L76K | Board | GPIO |
| --- | --- | --- |
| 3V3 / GND | 3V3 / GND | — |
| TX (D7) → | **RXD** | **12** (`GPS_UART_RX`) |
| RX (D6) ← | **TXD** | **11** (`GPS_UART_TX`) |
| WUP (D0) | **IO1** | **1** (`GPS_WAKEUP_PIN`) |

Module: Seeed **XIAO L76K** GNSS add-on, NMEA UART, baud **9600**. Soft-off: WUP low + pad hold. Wi‑Fi/BT: **external** antenna on Rufous.

Defines live in `include/board_config.h`. Do not use Qwiic UART (GPIO24/25 — conflicts with TP_RST / LCD_BLK).

---

## 3. Two-button UX (non-negotiable)

There is **no** on-device settings menu. Touch swipe is optional. Buttons stay the cabin input.

| Input | Default action | Timing |
| --- | --- | --- |
| GPIO0 short | Next page (or firmware-specific primary short) | Release before long |
| GPIO28 short | Previous page (or secondary short) | — |
| GPIO0 long **alone** | SoftAP enter / leave | ~2 s (`kLongPressMs`) |
| Both held | Soft power-off → deep sleep | ~3 s (`kSoftOffHoldMs`) |
| GPIO0 after GPIO wake | Confirm stay awake | ~1.5 s (`kWakeConfirmMs`) |

**Rules**

- No nested cabin menus that need select / back / adjust with two keys.
- Do **not** remap GPIO0 vs GPIO28 when **Flip Display 180** is on — the enclosure marks setup.
- Dual-hold must suppress the SoftAP long-press.
- Soft-off blocked during OTA. GPIO0 alone still leaves SoftAP while the AP is up.
- Prefer short-press for nav; do not add new chords that collide with 2 s / 3 s.
- Swipe (if CTP present) may mirror next/previous. Invert swipe when Flip is On. Buttons always work.
- Wardriver: start/stop logging belongs on SoftAP or a single dedicated short-press **only if** it stays one obvious cabin action — default plan puts export + clear on SoftAP ([`docs/PRODUCT.md`](docs/PRODUCT.md)).

Copy `.cursor/rules/two-button-nav.mdc` stays in this repo (SSID `TawniWardriver`).

---

## 4. Display (locked path — do not “simplify”)

1. Backlight GPIO25 HIGH first.
2. SPI2 @ **40 MHz**, DMA auto, transfer queue depth **1**.
3. `esp_lcd_new_panel_st7789`, `LCD_RGB_ENDIAN_BGR`, 16 bpp.
4. Manual RST GPIO23: HIGH 25 ms → LOW 25 ms → HIGH 125 ms (`reset_gpio_num = -1` on the panel).
5. Layout (`display_apply_layout`):
   - **Portrait** (default): `swap_xy(false)`, `mirror(false, false)`, gap **`(35, 0)`**, LVGL **170×320**
   - **Landscape**: `swap_xy(true)`, `mirror(true, false)`, gap **`(0, 35)`**, LVGL **320×170**
   - SoftAP saves layout to NVS and **reboots** to apply.
6. Wire wants **byte-swapped RGB565** (`to_panel_color` / `lv_draw_sw_rgb565_swap` in flush).

### LVGL

- LVGL 9.2, `-DLV_CONF_INCLUDE_SIMPLE`, `LV_COLOR_DEPTH 16`, `LV_MEM_SIZE (48 * 1024)`
- 170×320, `LV_DISPLAY_ROTATION_0`, **partial** render
- Draw buffer: **20 lines**, `MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`
- **Do not** full-frame PSRAM DMA — known to corrupt this SPI panel
- Blocking flush waits on SPI color-done ISR
- SoftAP: **suspend** the LVGL draw buffer to free the INTERNAL+DMA block if Wi‑Fi is tight

Cabin UI: high contrast, large type, one job per page, honest empty/stale states. Portrait (default) or landscape via SoftAP — reboot to apply. GPIO0 stays Start/Stop in both layouts.

---

## 5. Power, I2C, radios, GPS

### Soft-off / deep sleep

1. Radios down; GPS WUP (GPIO1) driven low and pad-held through deep sleep.
2. `display_enter_sleep()` — backlight off + ST7789 sleep.
3. `esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW)` — **GPIO0 only**.
4. `esp_deep_sleep_start()`.

Wake confirm: GPIO wakeup → hold GPIO0 ≥ 1.5 s or sleep again. USB/RST cold boot skips confirm.

Empty LiPo: AXP2602 Vbat ≤ **3.20 V** while discharging (current < −15 mA) for ~4 s → same soft-off. Do **not** sleep on USB/charge. Block soft-off during OTA.

### Touch (CST816S)

Same rules as VictronDash BSP. See [`docs/HARDWARE_HANDOVER.md`](docs/HARDWARE_HANDOVER.md).

### BLE vs Wi‑Fi vs SoftAP

ESP32-C5: **pause NimBLE scan before SoftAP**, and vice versa. Continuous BLE scan starves the AP. Wardriver also pauses active Wi‑Fi scan while SoftAP is up (export mode).

### GPS

- Parse NMEA (`$GNGGA` / `$GNRMC` or GPS-only equivalents) for fix, lat/lon, time when available.
- No fix → still log networks with empty / zero coords per Wigle conventions (document in PLAN).
- Never invent coordinates.

---

## 6. SoftAP + OTA (reuse the pattern)

Phone is the settings **and export** surface. One page. Not a cabin wizard.

**Feel**

1. First boot → SoftAP can already be on; cabin shows **SETUP MODE** + `TawniWardriver` (or a Live page + clear “no log yet” — see PRODUCT).
2. Join SSID → `http://192.168.4.1` → one page (status, Flip 180, logging controls, **Wigle CSV download**, Firmware, Stop hotspot).
3. Later: GPIO0 long toggles the AP.

**Always on that page**

- Version string
- **Screen** — Portrait / Landscape (reboots)
- Log status + download / clear (Wigle CSV)
- Firmware upload (app `.bin` only) + Factory reset (wipe NVS → SETUP MODE)
- Stop hotspot

MVP hotspot is **open** (no password) unless we later PIN it.

**OTA** — same dual-slot SoftAP pattern as VictronDash. Ship app `.bin` only. NVS survives OTA; Factory reset wipes. No ArduinoOTA, no phone-home GitHub OTA in MVP.

USB-C = first flash and recovery only.

---

## 7. Toolchain and first flash

**Prereqs:** Python 3.12+, PlatformIO Core, **Git for Windows on PATH**, USB-C cable.

```bash
pio run -e tawni
pio run -e tawni -t upload
pio device monitor -b 115200
```

(`env` name may become `rufous` later — keep one default env.)

**Download mode** if upload fails: hold **BOOT**, tap **RST**, release **BOOT**, upload again.

**Windows UTF-8 (required before upload):**

```powershell
chcp 65001
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"
pio run -e tawni -t upload
```

`Serial.setTxTimeoutMs(0)` so CDC does not block with no host attached.

---

## 8. Files to copy from Victron Instant Readout

Paths relative to `Firmware/Victron Instant Readout`.

### Copy / adapt (BSP)

| Path | Why |
| --- | --- |
| `boards/Lilygo-T-Display-C5.json` | Board definition |
| `include/board_config.h`, `include/board_pins.h` | Pins + portrait size (+ GPS pins when known) |
| `include/lv_conf.h` | LVGL 9.2 mem/color |
| `src/display.*`, `src/touch.*`, `src/ui/lvgl_port.*`, splash | LCD / touch / flush / brand |
| `lib/esp_lcd_st7789/`, `lib/CST816S/` | Drivers |
| Button / soft-off / wake slice of `src/main.cpp` | Timings + deep sleep |
| SoftAP **skeleton** in `src/softap/` | Pattern only — new SSID, new form, CSV download |
| `.cursor/rules/two-button-nav.mdc` | Agent UX rule |

### Do not copy

Victron decrypt, Victron BLE sniffer, Victron cabin gauges, Victron NVS device lists/secrets, Victron product docs.

### Suggested layout

```
TAWNI.md
README.md
platformio.ini
.gitignore
boards/
include/
lib/
src/main.cpp
src/display.*
src/touch.*
src/gps/          # ATGM336H UART + NMEA (new)
src/scan/         # Wi-Fi + BLE passive (new)
src/log/          # Wigle CSV in flash (new)
src/softap/
src/ui/
docs/
.cursor/rules/two-button-nav.mdc
```

Bring-up order: USB CDC → LCD + splash → buttons + soft-off → GPS NMEA → SoftAP + Flip + OTA → Wi‑Fi/BLE scan + Wigle log → polish cabin pages. Detail: [`docs/PLAN.md`](docs/PLAN.md).

---

## 9. GitHub — ready to push

Do **not** commit until the human asks. When they do:

1. Root `README.md`: Rufous-only, job, board, GPS module, SoftAP SSID, button map, build/flash.
2. `.gitignore`: `.pio/`, `*.elf`, secrets, **AI admin** not needed by flashers. Prefer Releases for `.bin`.
3. Version: `-DTAWNI_VERSION=\"X.Y.Z\"` + semver.
4. Smoke: flash → splash → GPS fix page honest → SoftAP → download CSV → SoftAP OTA.
5. No keys, no `.env`, no Victron leftovers, no agent scratch in the tree users clone.

USB first-flash / recovery; SoftAP for field updates. Device does not phone home.

---

## 10. Gotchas (read before “fixing” the BSP)

1. USB-CDC: `ARDUINO_USB_CDC_ON_BOOT=1` and `monitor_rts` / `monitor_dtr = 0`.
2. Portrait gap `(35, 0)` is required.
3. LVGL buffer = internal DMA strips, not PSRAM full frames.
4. Flip 180 = image only; GPIO0 stays the marked setup button.
5. Touch presence = `int_edges`, not idle `probe0x15`.
6. One `Wire.begin`; real CST816S RST pin.
7. Deep-sleep wake = GPIO0 only.
8. BLE scan vs SoftAP: one radio mode at a time; pause survey while exporting.
9. OneDrive paths can lock `.pio/build` during upload.
10. Dual-hold consumes GPIO0 long-press.
11. Windows upload: UTF-8 or esptool hangs the port.
12. **GPS UART GPIOs locked:** TXD=`GPIO11`, RXD=`GPIO12`, WUP=`GPIO1` (XIAO L76K). Not Qwiic 24/25.
13. Crossed TX/RX is the #1 GPS “no sentences” failure — check with a USB serial sniffer or `Serial` mirror.

---

## 11. Agent contract

- Keep C5 pin map / LCD init / USB-CDC flags unless re-validated on hardware.
- Answer for every feature: *how does the user reach this with two buttons?* If they cannot, SoftAP or defer.
- Do not port Victron decrypt. Do not add microSD pretence. Do not nest cabin menus.
- Do not ship active attack tools in MVP.
- Prefer copying proven display/touch/sleep/SoftAP code over rewriting it.
- Ask before `git commit` / `git push` unless the human already asked.
- Product north star: [`docs/PRODUCT.md`](docs/PRODUCT.md). Build order: [`docs/PLAN.md`](docs/PLAN.md).
