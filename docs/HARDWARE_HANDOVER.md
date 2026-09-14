# LILYGO T-Display C5 — hardware handover (Wardriver / Rufous)

Portable BSP notes for this Firmware. Sourced from Victron Instant Readout; treat C5 core as proven. **Rufous delta (GPS + external antenna) is new** — first unit wired 2026-09-09.

Keep C5 pin map, LCD init, and USB-CDC flags unless re-validated on hardware.

**Firmware briefing:** [`../TAWNI.md`](../TAWNI.md). Product: [`PRODUCT.md`](PRODUCT.md). Build order: [`PLAN.md`](PLAN.md).

---

## 0. SKU

| SKU | Hardware | This Firmware |
| --- | --- | --- |
| Tawni | C5 + LiPo + 2 buttons | Unsupported (no GPS) |
| **Rufous** | Tawni + **GPS** + **external Wi‑Fi/BT antenna** | **Required** |

---

## 1. Board identity (C5)

| Item | Value |
| --- | --- |
| Product | [LILYGO T-Display C5](https://www.lilygo.cc/products/t-display-c5) |
| MCU | ESP32-C5 (`esp32c5`) |
| Flash / PSRAM | 16 MB Quad flash, 8 MB Quad PSRAM (`BOARD_HAS_PSRAM`) |
| LCD | ST7789 IPS 1.9″ — portrait cabin **170×320** |
| Touch | CST816S (I2C `0x15`) — optional |
| PMIC | AXP2602 at I2C `0x62` |
| PlatformIO board | `Lilygo-T-Display-C5` |
| Board JSON | [`boards/Lilygo-T-Display-C5.json`](../boards/Lilygo-T-Display-C5.json) |
| Pin source of truth | [`include/board_config.h`](../include/board_config.h) + [`include/board_pins.h`](../include/board_pins.h) |

**Stack:** Arduino via `pioarduino/platform-espressif32` (stable zip), LVGL 9.2, vendored `lib/esp_lcd_st7789` + `lib/CST816S`. Partitions: `default_16MB.csv` (dual OTA).

---

## 2. Pin map (C5 core)

| Function | GPIO | Notes |
| --- | --- | --- |
| `BUTTON_PIN` (user / setup) | **0** | Active-low, `INPUT_PULLUP` |
| `BUTTON_BOOT` | **28** | Active-low, `INPUT_PULLUP` |
| LCD CS / SCK / MOSI | 26 / 7 / 9 | SPI; MISO unused |
| LCD DC / RST / BL | 8 / 23 / 25 | RST bit-banged; BL HIGH = on |
| I2C SDA / SCL | **2 / 3** | Touch + AXP |
| TP INT / RST | 27 / 24 | If `USE_TOUCH_SWIPE` |
| AXP2602 INT | 10 | Defined; unused in app |

USB: on-board USB-C → ESP32-C5 USB-CDC.

---

## 3. Rufous — XIAO L76K GPS

### Prototype wiring (current unit)

Seeed **GNSS add-on for XIAO** (Quectel **L76K**). Right-hand **12-pin header**, silk **GND · 3V3 · TXD · RXD**, plus **IO1**. Crossed UART so they talk. RESET (D2) left floating.

| XIAO silk | Board silk | ESP32-C5 GPIO | Role |
| --- | --- | --- | --- |
| 3V3 | 3V3 | — | Power (not 5 V) |
| GND | GND | — | Ground |
| D7 (GPS TX) | **RXD** | **GPIO12** (`U0RXD`) | Module → ESP |
| D6 (GPS RX) | **TXD** | **GPIO11** (`U0TXD`) | ESP → module |
| **WUP** (D0) | **IO1** | **GPIO1** (`GPS_WAKEUP_PIN`) | HIGH = run, LOW = standby |

Source: LilyGO schematic (`U0TXD, GPIO11` / `U0RXD, GPIO12`). Locked in [`include/board_config.h`](../include/board_config.h):

```c
#define GPS_UART_TX 11  // ESP TX → GPS RX (header TXD)
#define GPS_UART_RX 12  // ESP RX ← GPS TX (header RXD)
#define GPS_UART_BAUD 9600
#define GPS_WAKEUP_PIN 1  // L76K WUP; pad-hold LOW through deep sleep
```

**Do not** use the bottom UART Qwiic (**TX GPIO25 / RX GPIO24**) for GPS — those collide with LCD backlight and touch RST.

L76K ignores UART sleep. Soft-off drives **GPIO1 low** and `gpio_hold_en` so the pin stays low in ESP deep sleep (~360 µA module standby). `gps_begin()` releases the hold and drives WUP high.

### Driver expectations

- Hardware UART (not SoftSerial). Prefer a free UART host on ESP32-C5 that does not fight USB-CDC.
- NMEA 0183 text: prefer `RMC` + `GGA` (or GNSS `GNRMC` / `GNGGA`).
- Cabin must show honest **No fix** / **Fix** — never invent lat/lon.
- Soft-off: WUP low + GPIO hold; VCC stays on 3V3. UART `$PCAS12` is unused on this module.

### Bring-up test (when code exists)

1. USB monitor @ 115200.
2. Mirror raw NMEA to Serial for 30 s outdoors / near window.
3. Confirm checksum sentences and a 3D/2D fix field.
4. Crossed TX/RX → silence or garbage only.

---

## 4. Display (locked path)

Wardriver dual layout. See [`src/display.cpp`](../src/display.cpp). Portrait gap **`(35, 0)`**; landscape gap **`(0, 35)`**. SoftAP picks Portrait/Landscape and reboots. LVGL partial INTERNAL+DMA strips only — no full-frame PSRAM DMA.

**Button map:** GPIO0 = Start/Stop (portrait left / landscape bottom). GPIO28 = Page (portrait right / landscape top).

---

## 5. Buttons and soft power

| Input | Action | Timing |
| --- | --- | --- |
| GPIO0 short | Next page | release before long |
| GPIO28 short | Previous page | — |
| GPIO0 long **alone** | SoftAP enter / leave | ~2 s |
| Both held | Soft power-off → deep sleep | ~3 s |
| GPIO0 after GPIO wake | Confirm stay awake | ~1.5 s |

Deep-sleep wake: **GPIO0 only**. Soft-off blocked during OTA. Dual-hold suppresses SoftAP long-press.

---

## 6. Touch (optional CST816S)

Same as VictronDash: `-DUSE_TOUCH_SWIPE=1`, real `TP_RST`, presence = `int_edges`. Buttons remain primary cabin input.

---

## 7. Radios (Wardriver)

| Mode | Rule |
| --- | --- |
| Survey | Passive Wi‑Fi scan (2.4 + 5 GHz) + BLE scan |
| SoftAP export | **Pause** Wi‑Fi survey and NimBLE before bringing SoftAP up |
| Active tools | Out of MVP scope |

External antenna on Rufous is a hardware advantage for dual-band survey; firmware still uses the C5 Wi‑Fi/BLE stack.

---

## 8. PlatformIO starter

See [`platformio.ini`](../platformio.ini). USB-CDC flags and `monitor_rts` / `monitor_dtr = 0` are mandatory.

**Download mode:** hold **BOOT**, tap **RST**, release **BOOT**.

**Windows UTF-8 before upload:** `chcp 65001` + `$env:PYTHONUTF8="1"`.

---

## 9. Gotchas

1. USB-CDC reset if RTS/DTR not disabled.
2. `Serial.setTxTimeoutMs(0)`.
3. Portrait gap `(35, 0)`.
4. LVGL DMA: internal strips only.
5. Flip = image only.
6. Touch optional / `int_edges`.
7. Deep-sleep wake = GPIO0 only.
8. BLE vs SoftAP mutual exclusion.
9. OneDrive can lock `.pio/build`.
10. Soft-off vs long-press collision.
11. **GPS UART:** header TXD=`GPIO11`, RXD=`GPIO12`; XIAO D7→RXD, D6→TXD. WUP→GPIO1. Not the GPIO24/25 Qwiic UART.

---

## 10. Files to copy as BSP

From Victron Instant Readout: board JSON, `board_config` / `board_pins`, display/touch/lvgl_port/splash, LCD/touch libs, button/sleep timings, SoftAP **pattern** only.

Do not copy Victron decrypt, device NVS lists, or Victron cabin UI.
