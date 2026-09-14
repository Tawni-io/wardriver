# Wardriver (Rufous)

A [Tawni.io](https://tawni.io) firmware for **Rufous** hardware: LILYGO **T-Display C5** + **GPS** + external antenna, in the Tawni enclosure (2 buttons).

**Job:** Passive Wi‑Fi + BLE survey with GPS. Log **Wigle CSV** to flash. Export from SoftAP (`TawniWardriver`).

**Not for base Tawni** (no GPS). See [`TAWNI.md`](TAWNI.md). Current version: **[0.3.4](CHANGELOG.md)**.

## Hardware

| Item | Detail |
| --- | --- |
| Board | LILYGO T-Display C5 (ESP32-C5, 1.9″ ST7789, 170×320 portrait) |
| SKU | **Rufous** |
| GPS | Seeed XIAO L76K — 3.3 V, GND, UART crossed **TXD (GPIO11) / RXD (GPIO12)**, **WUP → GPIO1** |
| Radios | Dual-band Wi‑Fi + BLE; **external** antenna on Rufous |
| Storage | Onboard flash (no microSD) |

GPS UART: locked in `include/board_config.h` (see [`docs/HARDWARE_HANDOVER.md`](docs/HARDWARE_HANDOVER.md)).

## Buttons

Aligned with Gym Timer (bottom = GPIO0 marked setup, top = GPIO28):

| Input | Intent |
| --- | --- |
| Bottom short (GPIO0) | Start / stop logging |
| Bottom long ~2 s (GPIO0) | SoftAP enter / leave (`TawniWardriver`) |
| Top short (GPIO28) | Next page (Live / Recent / Info) |
| Top long ~2 s (GPIO28) | Previous page |
| Both hold ~3 s | Soft power-off |
| Bottom after wake ~1.5 s (GPIO0) | Stay on |

CSV download, clear log, and firmware update live on the phone SoftAP page.

## Build

Python 3.12+, PlatformIO Core, Git for Windows on PATH, USB-C to the T-Display C5.

```bash
pio run -e tawni
pio run -e tawni -t upload
pio device monitor -b 115200
```

Windows upload: `chcp 65001` then `$env:PYTHONUTF8="1"` before `pio … -t upload`. Download mode: hold **BOOT**, tap **RST**, release **BOOT**.

## Docs

| Doc | Role |
| --- | --- |
| [`TAWNI.md`](TAWNI.md) | Agent / product + board contract |
| [`docs/PRODUCT.md`](docs/PRODUCT.md) | North star (MVP vs later) |
| [`docs/PLAN.md`](docs/PLAN.md) | Bring-up order + open questions |
| [`docs/HARDWARE_HANDOVER.md`](docs/HARDWARE_HANDOVER.md) | Pins, LCD, Rufous GPS |

## Release builds

Public `.bin` files must be **`build_type = release`** (path-strip via `tools/pio_strip_host_paths.py`). Asset name: `wardriver-rufous-c5-vX.Y.Z.bin`.

## License

MIT — see [`LICENSE`](LICENSE). `lib/esp_lcd_st7789` vendored from [Xinyuan-LilyGO/T-Display-C5](https://github.com/Xinyuan-LilyGO/T-Display-C5).
