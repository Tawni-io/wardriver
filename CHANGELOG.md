# Changelog

On-device string: `TAWNI_VERSION` in `platformio.ini`.  
Release binaries: `wardriver-rufous-c5-vX.Y.Z.bin` (Rufous only).

Newest first.

## 0.3.4

First public Rufous release on [`Tawni-io/wardriver`](https://github.com/Tawni-io/wardriver/releases/tag/v0.3.4).

- Passive Wi‑Fi + BLE survey with GPS; Wigle CSV in flash; SoftAP export
- SoftAP SSID **TawniWardriver** (open MVP)
- Two-button map: bottom short start/stop logging; top short/long page next/prev; bottom long ~2 s SoftAP; both ~3 s soft-off; GPIO0 ~1.5 s after wake to stay on
- Release firmware (`build_type = release`, path-strip) — no host username paths in the `.bin`
