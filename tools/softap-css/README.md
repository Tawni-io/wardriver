# SoftAP DaisyUI CSS

Purged Tailwind CSS + DaisyUI for the Wardriver SoftAP portal.

```bash
npm install
npm run build
```

Writes `../../src/softap/portal_css.h` (PROGMEM). Keep `content.html` in sync with classes used in `src/softap/softap.cpp` so purge does not drop utilities (especially `flex-col`).

Do not serve this CSS from a CDN — SoftAP phones often have no internet for stylesheets.
