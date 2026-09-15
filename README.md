<div align="center">
  <img src="docs/images/tawni-logo.png" alt="Tawni" width="140">

  <p><strong>Wardriver</strong></p>

  <p>
    Passive Wi‑Fi + BLE survey with GPS — Wigle CSV in flash, SoftAP export.<br>
    Same pocket cabin, this firmware.
  </p>

  <p>
    <a href="https://github.com/Tawni-io/wardriver/releases"><strong>Releases</strong></a>
    ·
    <a href="https://tawni.io/wardriver.html"><strong>Product page</strong></a>
    ·
    <a href="https://tawni.io"><strong>tawni.io</strong></a>
  </p>

  <p>
    Current version: <strong>v0.3.4</strong>
    ·
    <a href="CHANGELOG.md">Changelog</a>
  </p>
</div>

---

## Table of Contents

- [About The Project](#-about-the-project)
- [Getting Started](#-getting-started)
- [Usage](#-usage)
- [Roadmap](#️-roadmap)
- [License](#-license)

---

## 📖 About The Project

<div align="center">
  <img src="docs/images/about.jpg" alt="Tawni cabin (same pocket hardware; Wardriver needs Rufous)" width="250">
</div>

<br>

> **Rufous only.** Needs GPS + external antenna. **Never** for base Tawni (no GPS).  
> Wrong box → no fix, no useful Wigle log.

Passive Wi‑Fi + BLE survey with GPS on the cabin. Flip pages with the two buttons — or a swipe if the touch screen is fitted. CSV export and settings live on your phone, not in a cabin menu.

| Page | Shows |
| --- | --- |
| Live | Logging on/off, GPS fix, live counts |
| Recent | Last hits |
| Info | Firmware version, battery, storage |

Survey is **passive only** (no deauth / attack tools). Logs are Wigle CSV in onboard flash (no microSD). No fix → empty coords (honest log).

### Built With

**Rufous only** (GPS + external antenna). Do **not** flash or sell this firmware for base Tawni.

Board: [LILYGO T-Display C5](https://www.lilygo.cc/products/t-display-c5) (ESP32-C5, 1.9″) on Rufous hardware.

---

## 🚀 Getting Started

Buyers: flash from [tawni.io](https://tawni.io) or a GitHub Release `.bin`. No PlatformIO required. Flash **Rufous** only.

### Install & update

#### USB — first flash or recovery

Use a USB-C cable and the flasher on [tawni.io](https://tawni.io), or flash `wardriver-rufous-c5-v0.3.4.bin` from [Releases](https://github.com/Tawni-io/wardriver/releases) with your usual ESP32 tool.

#### Phone — later updates

1. Download `wardriver-rufous-c5-vX.Y.Z.bin` from [Releases](https://github.com/Tawni-io/wardriver/releases) (or use the flasher on [tawni.io](https://tawni.io) when available)
2. Long-press the marked **setup** button (bottom, ~2 s)
3. Join Wi‑Fi **TawniWardriver** → open `http://192.168.4.1`
4. **Firmware** → upload the `.bin` → wait for reboot

Keep the unit powered during upload.

Asset name pattern: `wardriver-rufous-c5-vX.Y.Z.bin`

<!-- website:omit -->

#### Developer USB (PlatformIO)

Python 3.12+, [PlatformIO Core](https://platformio.org/install/cli), [Git](https://git-scm.com/downloads) on PATH, USB-C cable.

```bash
pio run -e tawni -t upload
pio device monitor -b 115200
```

If upload fails: hold **BOOT**, tap **RST**, release **BOOT**, then upload again.

**Windows:** run this before upload so the flash progress bar does not hang the COM port:

```powershell
chcp 65001
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"
pio run -e tawni -t upload
```

<!-- /website:omit -->

### Setup & export

Long-press the bottom (setup) button until the hotspot is on.

This firmware’s hotspot is **TawniWardriver**. Other Tawni firmwares use their own SSID so two boxes on the bench do not collide.

1. Join **TawniWardriver** (open network) → `http://192.168.4.1`
2. **Download Wigle CSV** or **Clear log** as needed
3. **Portrait / Landscape** if you want a different cabin layout (reboots to apply)
4. **Firmware** upload for field updates, or **Exit hotspot** when done

Logging starts and stops with the bottom short-press on the cabin. Export does not invent GPS coordinates.

---

## 🧭 Usage

Two buttons. The enclosure marks the **setup** button (bottom). Flip Display 180 does not swap them.

| Input | Action |
| --- | --- |
| Bottom short | Start / stop logging |
| Top short | Next page (Live / Recent / Info) |
| Top long (~2 s) | Previous page |
| Bottom long (~2 s) | Setup hotspot on/off (**TawniWardriver**) |
| Both held (~3 s) | Soft power-off (deep sleep) |
| Bottom after wake (~1.5 s) | Stay on |

Swipe left/right mirrors next/previous if the touch screen is fitted.

---

## 🗺️ Roadmap

- SoftAP OTA PIN / auth before wide promo (upload is open in v0.3.4, same pattern as Victron)
- Cabin / export polish from real Rufous drives

---

## 📄 License

Firmware: [MIT](LICENSE).

<!-- website:omit -->

## Build from source

```bash
pio run -e tawni
```

Field binary: `.pio/build/tawni/firmware.bin`  
Rename for a release: `wardriver-rufous-c5-vX.Y.Z.bin`

Public images (GitHub Releases, SoftAP, USB that leaves the bench) are **`build_type = release` only** — never `-ggdb2`. Path strip: `tools/pio_strip_host_paths.py`.

Version string: `-DTAWNI_VERSION` in `platformio.ini` (keep the `#ifndef` fallbacks in `src/main.cpp`, `src/softap/softap.cpp`, `src/ui/splash.cpp`, and cabin the same). Every GitHub Release must bump that string — splash, SoftAP, and Info all read it. Add a [CHANGELOG](CHANGELOG.md) entry, then tag `vX.Y.Z`.

## Developer docs

| Doc | What |
| --- | --- |
| [TAWNI.md](TAWNI.md) | Agent / product + board contract |
| [docs/PRODUCT.md](docs/PRODUCT.md) | North star (MVP vs later) |
| [docs/PLAN.md](docs/PLAN.md) | Bring-up order |
| [docs/HARDWARE_HANDOVER.md](docs/HARDWARE_HANDOVER.md) | Pins, LCD, Rufous GPS UART |

GPS UART (Rufous): TXD GPIO11 / RXD GPIO12 / WUP GPIO1 — locked; do not change without Hardware / orchestrator.

Vendored drivers keep their own licenses: [`lib/esp_lcd_st7789`](lib/esp_lcd_st7789) (LilyGO / João Brilha), [`lib/CST816S`](lib/CST816S) (Felix Biego).

<!-- /website:omit -->
