# Wardriver

A [Tawni.io](https://tawni.io) firmware face for the **LILYGO T-Display C5** (ESP32-C5, 1.9″ ST7789, 320×170 landscape).

**Job:** Wi‑Fi + BLE survey. **Firmware is TBD** — this repo holds the proven board support (LCD, buttons, splash). No onboard GPS or microSD; log SSIDs/BLE to flash and export from SoftAP. Dual-band scan is the C5 hook vs typical 2.4-only ESP toys.

Board + two-button + SoftAP contract: [`TAWNI.md`](TAWNI.md). Hardware appendix: [`docs/HARDWARE_HANDOVER.md`](docs/HARDWARE_HANDOVER.md).

## Planned two-button fit

| Input | Intent (when firmware lands) |
| --- | --- |
| GPIO0 short | Next page (live / log / stats) |
| GPIO28 short | Previous |
| GPIO0 long (~2 s) | SoftAP (`TawniWardriver`) — export |
| Both hold (~3 s) | Soft power-off |

## Build

Python 3.12+, PlatformIO Core, Git for Windows on PATH, USB-C to the T-Display C5.

```bash
pio run -e tawni
pio run -e tawni -t upload
pio device monitor -b 115200
```

Windows upload: `chcp 65001` then `$env:PYTHONUTF8="1"` before `pio … -t upload`. Download mode: hold **BOOT**, tap **RST**, release **BOOT**.

## License notes

- Face firmware: TBD
- `lib/esp_lcd_st7789` vendored from [Xinyuan-LilyGO/T-Display-C5](https://github.com/Xinyuan-LilyGO/T-Display-C5)
