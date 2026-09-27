# Changelog

On-device string: `TAWNI_VERSION` in `platformio.ini`.  
Release binaries: `wardriver-rufous-c5-vX.Y.Z.bin` (Rufous only).

Newest first.

## 0.4.0

SoftAP map + DaisyUI portal on Rufous.

- SoftAP portal restyled with **DaisyUI** (purged Tailwind CSS served from the device at `/portal.css` — no CSS CDN). Rebuild: `tools/softap-css` → `src/softap/portal_css.h`
- Portal order: Status → Map → Log → Screen → Phone internet for map tiles → Firmware → Exit → Danger zone (collapsed)
- SoftAP **map** (`/map`): Leaflet + OSM (CDN for tiles/scripts); last **500** geolocated hits; loading spinner; Wi‑Fi / BLE filters
- Optional **join phone hotspot** (STA portal) so map tiles can load; cabin setup: Broadcasting as AP vs Connected to WiFi; spinner on SoftAP↔STA transitions
- SoftAP SSID **TawniWardriver** (shipping law)
- Clear log / factory reset stay confirm-gated and visually secondary

## 0.3.4

First public Rufous release on [`Tawni-io/wardriver`](https://github.com/Tawni-io/wardriver/releases/tag/v0.3.4).

- Passive Wi‑Fi + BLE survey with GPS; Wigle CSV in flash; SoftAP export
- SoftAP SSID **TawniWardriver** (open MVP)
- Two-button map: bottom short start/stop logging; top short/long page next/prev; bottom long ~2 s SoftAP; both ~3 s soft-off; GPIO0 ~1.5 s after wake to stay on
- Release firmware (`build_type = release`, path-strip) — no host username paths in the `.bin`
