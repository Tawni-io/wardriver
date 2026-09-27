# Changelog

On-device string: `TAWNI_VERSION` in `platformio.ini`.  
Release binaries: `wardriver-rufous-c5-vX.Y.Z.bin` (Rufous only).

Newest first.

## Unreleased

Drop SoftAP map and external AP join (survey cannot run at the same time; rethink later).

- Remove `/map`, `/api/points`, `/api/gps`, Leaflet UI (`map_ui.*`)
- Remove STA portal / phone-hotspot join (`/wifi/*`, prefs `wssid`/`wpass`, cabin “Connected to WiFi”)
- SoftAP portal: Status → Log → Screen → Firmware → Exit → Danger zone
- Trim DaisyUI CSS safelist (no map chrome); clear leftover Wi‑Fi join NVS on boot

## 0.4.0

SoftAP DaisyUI portal on Rufous (map + STA join shipped here; removed in Unreleased).

- SoftAP portal restyled with **DaisyUI** (purged Tailwind CSS served from the device at `/portal.css` — no CSS CDN). Rebuild: `tools/softap-css` → `src/softap/portal_css.h`
- Portal order (at ship): Status → Map → Log → Screen → Phone internet for map tiles → Firmware → Exit → Danger zone (collapsed)
- SoftAP SSID **TawniWardriver** (shipping law)
- Clear log / factory reset stay confirm-gated and visually secondary

## 0.3.4

First public Rufous release on [`Tawni-io/wardriver`](https://github.com/Tawni-io/wardriver/releases/tag/v0.3.4).

- Passive Wi‑Fi + BLE survey with GPS; Wigle CSV in flash; SoftAP export
- SoftAP SSID **TawniWardriver** (open MVP)
- Two-button map: bottom short start/stop logging; top short/long page next/prev; bottom long ~2 s SoftAP; both ~3 s soft-off; GPIO0 ~1.5 s after wake to stay on
- Release firmware (`build_type = release`, path-strip) — no host username paths in the `.bin`
