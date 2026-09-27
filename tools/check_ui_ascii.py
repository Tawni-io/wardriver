#!/usr/bin/env python3
"""Fail if LVGL/cabin UI call sites use non-ASCII (Montserrat shows tofu boxes)."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
FILES = [
    ROOT / "src" / "main.cpp",
    ROOT / "src" / "ui" / "cabin.cpp",
    ROOT / "src" / "ui" / "splash.cpp",
]

# Display-bound call sites only (Serial logs may keep Unicode).
CALL_RE = re.compile(
    r"(?:cabin_show_(?:busy|message|setup)|portal_show_busy|splash_set_status|"
    r"lv_label_set_text)\s*\((.*?)\)\s*;",
    re.S,
)
STR_RE = re.compile(r'"([^"\\]|\\.)*"')

bad = []
for path in FILES:
    text = path.read_text(encoding="utf-8-sig")
    for cm in CALL_RE.finditer(text):
        args = cm.group(1)
        for sm in STR_RE.finditer(args):
            s = sm.group(0)
            if any(ord(c) > 127 for c in s):
                line = text.count("\n", 0, cm.start()) + 1
                bad.append(f"{path.relative_to(ROOT)}:{line}: {s}")

if bad:
    print("Non-ASCII display strings (cabin font cannot draw these):")
    for b in bad:
        print(" ", b)
    sys.exit(1)
print("OK: display-bound string literals are ASCII-only")
