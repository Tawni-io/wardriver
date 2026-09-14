# Tawni — Wardriver

Passive Wi‑Fi + BLE survey with GPS. Same pocket box idea as other Tawni firmwares — this one is **Rufous only**.

> **Rufous only.** Needs GPS + external antenna. **Never** for base Tawni (no GPS).  
> Wrong box → no fix, no useful Wigle log.

**Hardware:** [LILYGO T-Display C5](https://www.lilygo.cc/products/t-display-c5) (ESP32-C5, 1.9″) on **Rufous** (GPS + external antenna).  
**Product:** [tawni.io](https://tawni.io)

Current version: **v0.3.4** — [changelog](CHANGELOG.md) · [releases](https://github.com/Tawni-io/wardriver/releases)

Release asset: `wardriver-rufous-c5-v0.3.4.bin`

---

## What this firmware does

| Job | Detail |
| --- | --- |
| Survey | Passive Wi‑Fi + BLE scan (no deauth / attack tools) |
| GPS | Fix when available; empty coords if no fix (honest log) |
| Log | Wigle CSV stored in onboard flash (no microSD) |
| Export | SoftAP download / clear on your phone |

Cabin pages: **Live** (logging + fix + counts), **Recent** (last hits), **Info** (version, battery, storage). Settings and CSV live on the phone, not in a cabin menu.

---

## Buttons

Two buttons. The enclosure marks the **setup** button (bottom). Flip Display 180 does not swap them.

| Input | Action |
| --- | --- |
| Bottom short | Start / stop logging |
| Top short | Next page (Live / Recent / Info) |
| Top long (~2 s) | Previous page |
| Bottom long (~2 s) | Setup hotspot on/off |
| Both held (~3 s) | Soft power-off (deep sleep) |
| Bottom after wake (~1.5 s) | Stay on |

Swipe left/right mirrors next/previous if the touch screen is fitted.

---

## Install & update

### USB — first flash or recovery

Use a USB-C cable and the flasher on [tawni.io](https://tawni.io) when available, or flash `wardriver-rufous-c5-v0.3.4.bin` from [Releases](https://github.com/Tawni-io/wardriver/releases) with your usual ESP32 tool.

Flash **Rufous** only. Base Tawni is not supported.

### Phone — later updates

1. Download `wardriver-rufous-c5-vX.Y.Z.bin` from [Releases](https://github.com/Tawni-io/wardriver/releases) (or use the flasher on [tawni.io](https://tawni.io) when available)
2. Long-press the marked setup button (~2 s)
3. Join Wi‑Fi **TawniWardriver** → open `http://192.168.4.1`
4. **Firmware** → upload the `.bin` → wait for reboot

Keep the unit powered during upload.

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

---

## Setup & export

Long-press the bottom (setup) button until the hotspot is on. Cabin shows setup / export mode.

This firmware’s hotspot is **TawniWardriver**. Other Tawni firmwares use their own SSID so two boxes on the bench do not collide.

1. Join **TawniWardriver** (open network) → `http://192.168.4.1`
2. **Download Wigle CSV** or **Clear log** as needed
3. **Portrait / Landscape** if you want a different cabin layout (reboots to apply)
4. **Firmware** upload for field updates, or **Exit hotspot** when done

Logging starts and stops with the bottom short-press on the cabin. Export does not invent GPS coordinates.

---

## License

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
