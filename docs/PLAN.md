# Wardriver — implementation plan

Build order and open questions. Product north star: [`PRODUCT.md`](PRODUCT.md). Hardware: [`HARDWARE_HANDOVER.md`](HARDWARE_HANDOVER.md).

**Do not start feature code until GPS UART GPIOs are locked** (or a dedicated bring-up sketch is used to discover them).

---

## 1. Current repo state (2026-09-14)

| Area | Status |
| --- | --- |
| C5 BSP files (display, touch, splash, board JSON) | Present |
| `src/main.cpp` + cabin / SoftAP / GPS / survey / Wigle | MVP in tree (**v0.3.4**) |
| SoftAP / OTA | `TawniWardriver` — Flip, CSV export, OTA |
| GPS / scan / Wigle log | Wired + logging |
| Rufous | GPS + external antenna (this firmware only) |

---

## 2. Bring-up phases

### Phase A — Docs / contract (this pass)

- [x] Rufous-only + passive + Wigle + SoftAP decisions  
- [x] `TAWNI.md` / README / PRODUCT / HARDWARE / PLAN  
- [x] Confirm **GPS RX/TX GPIO** + baud from the wired unit → write into `board_config.h` (GPIO11/12, baud 9600 assumed)

### Phase B — BSP life on device

1. [x] Wire `main` to display + LVGL + splash (copy Victron pattern).  
2. [x] Buttons: next/prev stubs, SoftAP long-press, soft-off + wake confirm.  
3. [x] SoftAP skeleton: SSID `TawniWardriver`, Flip 180, version, Stop hotspot, OTA upload.  
4. [x] Smoke on USB-connected Rufous (v0.1.0 flashed).

### Phase C — GPS

1. [x] Lock pins; open UART; dump raw NMEA to USB Serial.  
2. [x] Parse RMC/GGA (inc. GN* talkers); expose fix struct to UI.  
3. [x] Cabin **Live**: Fix / No fix / searching; lat/lon only when valid.

### Phase D — Passive survey + log

1. [x] Wi‑Fi scan loop (2.4 + 5); dedupe / refresh policy.  
2. [x] BLE scan (pause when SoftAP up).  
3. [x] Append **Wigle CSV** rows to LittleFS (~3.4 MB partition).  
4. [x] SoftAP: download file + clear + start/stop logging.  
5. [x] Cabin Recent + counts.

### Phase E — Polish

1. Storage watermarks / “log full” honesty.  
2. Power: survey vs soft-off; empty LiPo.  
3. README flash instructions + first GitHub Release shape.  
4. Legal/README tone: passive survey tool; user responsible for local law.

---

## 3. Suggested cabin pages (MVP)

| Page | Content |
| --- | --- |
| Live | Logging on/off · GPS state · Wi‑Fi n · BLE n · band hint |
| Recent | Last few SSIDs / BLE names (scroll not required — short list) |
| Info | Version · Rufous · LiPo · free log space · SoftAP hint |

Start/stop logging: **SoftAP primary**; optional later: single obvious cabin gesture if it does not collide with SoftAP long-press.

---

## 4. Wigle CSV (MVP)

- Emit a header compatible with Wigle Wi‑Fi/Bluetooth CSV import.  
- Wi‑Fi and BLE both present in the downloadable file (Wigle-compatible type columns).  
- Timestamp: prefer GPS time when fix; else device uptime/RTC policy documented (ESP32-C5 may lack battery RTC — prefer GPS time, else leave empty or use scan epoch after SoftAP sets time — **decide in Phase D**).  
- No fix: follow Wigle empty/zero coordinate conventions; do not fake a location.

Exact column list: lock against current Wigle template during Phase D (link in README then).

---

## 5. Radio / SoftAP state machine

```
SURVEY  --(GPIO0 long)-->  EXPORT (SoftAP)
EXPORT  --(Stop / GPIO0 long)-->  SURVEY

SURVEY: Wi-Fi scan + BLE scan + GPS parse + append log (if logging on)
EXPORT: radios survey off; SoftAP on; LVGL buffer suspend if needed; serve CSV
```

Never run continuous NimBLE + SoftAP together on C5.

---

## 6. Open questions (need human)

| # | Question | Why it blocks |
| --- | --- | --- |
| 1 | ~~Which C5 GPIOs are GPS RX and TX?~~ | **Locked:** TXD=GPIO11, RXD=GPIO12, crossed to ATGM336H |
| 2 | ATGM336H baud (9600 vs 115200)? | UART config — try 9600 first |
| 3 | GPS antenna type fitted (ceramic on module vs external)? | Field expectations only |
| 4 | One continuous CSV vs session files? | SoftAP UI + flash layout |
| 5 | Keep log across OTA / reboot by default? | NVS + filesystem policy |
| 6 | Rename PlatformIO env `tawni` → `rufous`? | Cosmetics / clarity |

---

## 7. Bench notes (USB Rufous)

When testing:

```powershell
chcp 65001
$env:PYTHONUTF8 = "1"
pio device list
pio device monitor -b 115200
```

MVP **v0.3.4**: splash → cabin pages → GPS fix honesty → SoftAP CSV export. Monitor for NMEA / survey / SoftAP lines as needed.

---

## 8. Public repo hygiene

Flashers need: README, license, source, release `.bin` instructions.  
Gitignore AI admin / local scratch (see root `.gitignore`). Keep `.cursor/rules/two-button-nav.mdc` — it encodes product UX, not private process noise.
