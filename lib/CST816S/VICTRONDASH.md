# CST816S (LilyGO T-Display-C5 fork)

Vendored from [Xinyuan-LilyGO/T-Display-C5 `lib/CST816S`](https://github.com/Xinyuan-LilyGO/T-Display-C5/tree/master/lib/CST816S)
(fbiego-based, with `setRotation()`).

## VictronDash patch

`CST816S::begin(TwoWire&, int)` **does not** call `Wire.begin()`. The application must
`Wire.begin(sda, scl)` once before `begin()`. A second `Wire.begin()` on ESP32 Arduino 3.x
returns `ESP_ERR_INVALID_STATE` and breaks the bus.

RST is still pulsed when `rst >= 0`. Pass real `TP_RST` (GPIO 24) — do not use `-1` on ESP32-C5
(it becomes GPIO 255).

Original upstream README follows below.

---
