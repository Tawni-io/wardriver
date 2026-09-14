# Remap __FILE__ / debug prefixes so firmware .bin assets do not embed
# C:/Users/<host>/.platformio/... from the build machine.
#
# Hard rule: public images are release-only. Cabin envs must not be
# `build_type = debug` (touch gold-tests may). Release mode still needs these
# prefix maps: Arduino/ESP_LOG puts __FILE__ in .rodata.
# Wire as `extra_scripts = pre:tools/pio_strip_host_paths.py` on every firmware.

Import("env")  # noqa: F821 — PlatformIO / SCons

import os
from pathlib import Path

_pioenv = str(env.get("PIOENV", "")).lower()
try:
    _build_type = str(env.GetProjectOption("build_type") or "release").lower()
except Exception:
    _build_type = "release"
if "touch" not in _pioenv and _build_type == "debug":
    print(
        "ERROR: %s is public firmware; build_type=debug is forbidden. "
        "Use a *touch env for local debug, or set build_type=release."
        % env.get("PIOENV")
    )
    env.Exit(1)


def _slash_variants(path):
    if not path:
        return []
    raw = str(path).rstrip("/\\")
    out = []
    for p in (raw, raw.replace("\\", "/"), raw.replace("/", "\\")):
        if p and p not in out:
            out.append(p)
    return out


def _add_prefix_maps(old, new="."):
    for p in _slash_variants(old):
        env.Append(
            CCFLAGS=[
                "-fmacro-prefix-map=%s=%s" % (p, new),
                "-ffile-prefix-map=%s=%s" % (p, new),
            ]
        )


_add_prefix_maps(env.get("PROJECT_DIR"))
_add_prefix_maps(
    os.environ.get("PLATFORMIO_CORE_DIR") or env.get("PROJECT_CORE_DIR")
)
_add_prefix_maps(str(Path.home()))
