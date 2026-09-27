# Product focus — Wardriver (Rufous)

North star for planning and build order. Detail lives in [`PLAN.md`](PLAN.md) and [`../TAWNI.md`](../TAWNI.md). **This file wins** when something feels overbuilt.

**Audience:** people who want a pocket dual-band Wiâ€‘Fi + BLE logger with GPS, in the same rugged Tawni-family box — flash, walk/drive, download a Wigle file on the phone. Not a Marauder clone for day one.

---

## What they care about

1. **Am I logging?** (clear on/off + storage health)
2. **Do I have GPS?** (honest fix / no-fix)
3. **What am I seeing?** (counts, recent SSIDs / BLE — glanceable)
4. **Get the file off the device** without a laptop (SoftAP → Wigle CSV)
5. **Update firmware** from the phone

Everything else waits.

---

## Hardware constraint

| SKU | Use this Firmware? |
| --- | --- |
| **Rufous** (C5 + GPS + external antenna + 2 buttons) | **Yes** |
| Base **Tawni** (no GPS) | **No** |

MVP storage = **flash only** (no microSD). Export = SoftAP download.

---

## MVP product (ship this first)

| Slice | Keep |
| --- | --- |
| Hardware | Rufous: T-Display C5 + ATGM336H UART GPS + external antenna |
| Cabin pages | **Live** (logging + fix + counts) Â· **Recent** (last hits) Â· **Info** (version, LiPo, storage) |
| Buttons | **Gym-aligned:** bottom short = log on/off Â· bottom long ~2 s SoftAP Â· top short = next page Â· top long ~2 s = previous Â· both ~3 s soft-off |
| Display flip | SoftAP **Screen: Portrait / Landscape** (save + reboot). Buttons keep the same jobs. |
| Radios | Passive Wiâ€‘Fi (2.4 + 5) + BLE scan |
| GPS | NMEA fix → geotag rows when available |
| Log format | **Wigle CSV** (Wiâ€‘Fi + BLE rows) |
| SoftAP | Open `TawniWardriver` → DaisyUI portal: Status, Log, Screen, Firmware, Exit, Danger zone |
| Updates | Phone → SoftAP Firmware section → app `.bin` |
| Out of scope | Deauth, Evil Portal, packet injection, SD card, Tawni SKU support |

### SoftAP — one page

Join `TawniWardriver` → `http://192.168.4.1`:

1. **Status** — version, survey paused, logging, GPS, log size / free space, portal URL  
2. **Log** — Download Wigle CSV · Clear log (confirm; secondary)  
3. **Screen** — Portrait (default) / Landscape — reboots to apply  
4. **Firmware** — upload app `.bin`  
5. **Exit portal** · **Danger zone** (factory reset, collapsed)

UI: DaisyUI theme; CSS served from the device (`/portal.css`). SoftAP is setup/export only — survey radios pause while the portal is up. On-device map / STA join deferred until a dual-radio design exists.

**Cabin while SoftAP is up:** **Broadcasting as AP** with the URL. Survey radios paused.

### Cabin should feel like

1. Power on → splash → **Live** page: Logging Â· GPS Â· Wiâ€‘Fi count Â· BLE count  
2. Short presses cycle Live / Recent / Info  
3. Long-press marked button → SoftAP for download / settings  
4. Dual-hold → soft-off  

No nested menus. No “select network to attack.”

---

## Keep capable, but not in the user’s Firmware

| Feature | Stance |
| --- | --- |
| Marauder-style active tools | **Later / maybe never** on this Firmware — separate product decision |
| Filter bands / channels | SoftAP advanced fold later; MVP = sensible full passive scan |
| Multiple log files / sessions | Later — MVP one rotating or single file with clear |
| SoftAP password | MVP open hotspot |
| Touch swipe | Optional mirror of next/prev; buttons enough |
| Upload to Wigle from device | No — user downloads CSV and uploads themselves |
| Auto GitHub OTA | No in MVP |
| Board LiPo on Live | Small indicator OK; detail on Info |

---

## Success criteria (MVP)

- Rufous outdoors: GPS fix within a few minutes in open sky (module-dependent).  
- Walk a block: Wiâ€‘Fi + BLE rows appear in downloaded Wigle CSV with timestamps; coords when fix held.  
- SoftAP OTA of the same app `.bin` works; NVS/log policy documented (clear vs keep).  
- Two-button UX never requires a third “select” key.

---

## Non-goals (say no)

- Marketing or flashing this Firmware onto base Tawni.  
- Shipping agent/admin clutter in the public GitHub tree.  
- Porting Victron Instant Readout code.  
- Building a full Marauder UI on a 170Ã—320 two-button cabin.
