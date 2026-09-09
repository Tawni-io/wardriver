#include "touch.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "board_config.h"

namespace {

constexpr uint8_t kAxpAddr = 0x62;
constexpr uint8_t kAxpRegId = 0x00;
constexpr uint8_t kAxpRegMode = 0x02;
constexpr uint8_t kAxpRegVbatH = 0x04;
constexpr uint8_t kAxpRegSoc = 0x08;
constexpr uint8_t kAxpRegIbatH = 0x14;
constexpr uint8_t kAxpChipId = 0x1C;
// Default sense resistor 10 mΩ (LilyGO): I_mA = raw * 2.5 / 10.
constexpr float kAxpIbatLsbMa = 0.25f;

bool g_axp_present = false;
bool g_axp_probed = false;

bool axp_probe_addr(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool axp_read_reg(uint8_t reg, uint8_t* value) {
  if (!value) return false;
  Wire.beginTransmission(kAxpAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(kAxpAddr, (uint8_t)1) != 1) return false;
  *value = Wire.read();
  return true;
}

bool axp_read_reg16_be(uint8_t reg, uint16_t* value) {
  if (!value) return false;
  Wire.beginTransmission(kAxpAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(kAxpAddr, (uint8_t)2) != 2) return false;
  const uint8_t hi = Wire.read();
  const uint8_t lo = Wire.read();
  *value = (uint16_t)((hi << 8) | lo);
  return true;
}

bool axp_probe_wake(void) {
  if (!axp_probe_addr(kAxpAddr)) {
    Serial.println("AXP2602: not on bus (0x62)");
    g_axp_present = false;
    g_axp_probed = true;
    return false;
  }

  uint8_t id = 0;
  if (!axp_read_reg(kAxpRegId, &id)) {
    g_axp_present = false;
    g_axp_probed = true;
    return false;
  }
  if (id != kAxpChipId) {
    Serial.printf("AXP2602: unexpected ID 0x%02X (want 0x%02X)\n", id, kAxpChipId);
    g_axp_present = false;
    g_axp_probed = true;
    return false;
  }

  uint8_t mode = 0;
  if (axp_read_reg(kAxpRegMode, &mode)) {
    mode &= ~0x01;
    Wire.beginTransmission(kAxpAddr);
    Wire.write(kAxpRegMode);
    Wire.write(mode);
    Wire.endTransmission();
  }

  Serial.println("AXP2602: probed OK (wake best-effort)");
  g_axp_present = true;
  g_axp_probed = true;
  return true;
}

}  // namespace

bool board_power_present(void) { return g_axp_present; }

bool board_power_read(int* mV, int* soc_pct, int* i_mA) {
  if (!g_axp_present) return false;

  uint16_t raw = 0;
  if (!axp_read_reg16_be(kAxpRegVbatH, &raw)) return false;
  // LilyGO AXP2602: low 14 bits; 1 LSB ≈ 1 mV.
  const int voltage_mV = (int)(raw & 0x3FFF);
  if (mV) *mV = voltage_mV;

  if (soc_pct) {
    uint8_t soc = 0;
    if (axp_read_reg(kAxpRegSoc, &soc) && soc <= 100) {
      *soc_pct = (int)soc;
    } else {
      *soc_pct = -1;
    }
  }

  if (i_mA) {
    uint16_t iraw = 0;
    if (axp_read_reg16_be(kAxpRegIbatH, &iraw)) {
      const int16_t signed_raw = (int16_t)iraw;
      *i_mA = (int)lroundf((float)signed_raw * kAxpIbatLsbMa);
    } else {
      *i_mA = 0;
    }
  }
  return true;
}

#if USE_TOUCH_SWIPE

#include <CST816S.h>

namespace {

// Real TP_RST — rst=-1 becomes GPIO 255 on ESP32-C5 and breaks I2C.
CST816S g_tp(IIC_SDA_PIN, IIC_SCL_PIN, TP_RST, TP_INT);
bool g_armed = false;
bool g_confirmed = false;
bool g_saw_0x15 = false;
bool g_nav_flip = false;
volatile uint32_t g_int_edges = 0;

void IRAM_ATTR on_tp_irq(void) {
  uint32_t n = g_int_edges;
  g_int_edges = n + 1;
}

bool probe_addr(uint8_t addr) { return axp_probe_addr(addr); }

void i2c_scan(const char *tag) {
  Serial.printf("I2C scan (%s):", tag);
  int n = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    if (probe_addr(addr)) {
      Serial.printf(" 0x%02X", addr);
      n++;
      if (addr == CST816S_ADDRESS) g_saw_0x15 = true;
    }
  }
  if (n == 0) Serial.print(" (none)");
  Serial.println();
}

/** Disable CST816S auto-sleep while the chip is awake after RST (reg 0xFE = 0x07). */
void write_sleep_disable(void) {
  Wire.beginTransmission(CST816S_ADDRESS);
  Wire.write(0xFE);
  Wire.write(0x07);
  Wire.endTransmission();
}

bool i2c_read_regs(uint8_t reg, uint8_t *out, size_t len) {
  Wire.beginTransmission(CST816S_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom((uint8_t)CST816S_ADDRESS, (uint8_t)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) out[i] = Wire.read();
  return true;
}

bool read_gesture(uint8_t *gest, int *x, int *y) {
  uint8_t d[6];
  if (!i2c_read_regs(0x01, d, 6)) return false;
  *gest = d[0];
  *x = ((d[2] & 0x0F) << 8) | d[3];
  *y = ((d[4] & 0x0F) << 8) | d[5];
  return true;
}

void best_effort_irq_config(void) {
  Wire.beginTransmission(CST816S_ADDRESS);
  Wire.write(0xFA);
  Wire.write(0x70);
  Wire.endTransmission();
  write_sleep_disable();
}

const char *gest_name(uint8_t gid) {
  switch (gid) {
    case SWIPE_UP: return "SWIPE UP";
    case SWIPE_DOWN: return "SWIPE DOWN";
    case SWIPE_LEFT: return "SWIPE LEFT";
    case SWIPE_RIGHT: return "SWIPE RIGHT";
    case SINGLE_CLICK: return "SINGLE CLICK";
    case DOUBLE_CLICK: return "DOUBLE CLICK";
    case LONG_PRESS: return "LONG PRESS";
    case NONE: return "NONE";
    default: return "UNKNOWN";
  }
}

TouchNav map_nav(uint8_t gid) {
  TouchNav nav = kTouchNavNone;
  switch (gid) {
    case SWIPE_LEFT:
    case SWIPE_UP:
      nav = kTouchNavNext;
      break;
    case SWIPE_RIGHT:
    case SWIPE_DOWN:
      nav = kTouchNavPrev;
      break;
    default:
      return kTouchNavNone;
  }
  if (g_nav_flip) {
    return (nav == kTouchNavNext) ? kTouchNavPrev : kTouchNavNext;
  }
  return nav;
}

}  // namespace

bool touch_init(void) {
  g_armed = false;
  g_confirmed = false;
  g_saw_0x15 = false;
  g_int_edges = 0;

  // Single Wire.begin — vendored CST816S::begin must not call begin again.
  Wire.begin(IIC_SDA_PIN, IIC_SCL_PIN);
  Wire.setClock(400000);

  i2c_scan("pre-AXP");
  axp_probe_wake();

  pinMode(TP_INT, INPUT_PULLUP);

  // FALLING first (active-low IRQ). Library pulses TP_RST then attaches IRQ.
  g_tp.begin(FALLING);
  Wire.setClock(400000);

  // Disable auto-sleep immediately after RST while the chip may still be awake.
  write_sleep_disable();
  g_tp.attachUserInterrupt(on_tp_irq);

  i2c_scan("post-CST816S-begin");
  best_effort_irq_config();
  g_armed = true;

  Serial.printf("Touch: armed INT=%d edges=%lu saw_0x15=%d (FALLING IRQ)\n", digitalRead(TP_INT),
                (unsigned long)g_int_edges, (int)g_saw_0x15);
  if (!g_saw_0x15) {
    Serial.println("Touch: CST816S (0x15) NACK at idle is normal until wake; watch int_edges");
  }
  return true;
}

void touch_set_nav_flip(bool flip) {
  g_nav_flip = flip;
}

TouchNav touch_poll(void) {
  if (!g_armed) return kTouchNavNone;

  uint8_t gest = 0;
  int x = 0;
  int y = 0;
  bool got = false;

  if (g_tp.available()) {
    gest = g_tp.data.gestureID;
    x = g_tp.data.x;
    y = g_tp.data.y;
    got = true;
  } else {
    static bool was_low = false;
    const bool low = digitalRead(TP_INT) == LOW;
    if (low && !was_low) {
      got = read_gesture(&gest, &x, &y);
    }
    was_low = low;

    // Poll I2C periodically — CST816S may only ACK after a touch event.
    if (!got) {
      static uint32_t last_poll_ms = 0;
      const uint32_t now = millis();
      if ((now - last_poll_ms) >= 150) {
        last_poll_ms = now;
        if (probe_addr(CST816S_ADDRESS)) {
          g_saw_0x15 = true;
          got = read_gesture(&gest, &x, &y);
        }
      }
    }
  }

  if (!got) return kTouchNavNone;

  g_saw_0x15 = true;
  if (!g_confirmed) {
    g_confirmed = true;
    best_effort_irq_config();
    Serial.println("Touch: CST816S responding");
  }

  Serial.printf("Touch: gest=%s (%u) x=%d y=%d\n", gest_name(gest), (unsigned)gest, x, y);
  return map_nav(gest);
}

bool touch_armed(void) { return g_armed; }

bool touch_confirmed(void) { return g_confirmed; }

void touch_print_status(void) {
  if (!g_armed) {
    Serial.println("Touch: off");
    return;
  }
  const bool probe = probe_addr(CST816S_ADDRESS);
  if (probe) g_saw_0x15 = true;
  Serial.printf("Touch: INT=%d confirmed=%d saw_0x15=%d probe0x15=%d int_edges=%lu\n",
                digitalRead(TP_INT), (int)g_confirmed, (int)g_saw_0x15, (int)probe,
                (unsigned long)g_int_edges);
}

#endif  // USE_TOUCH_SWIPE
