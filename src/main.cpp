#include <Arduino.h>

#ifndef TAWNI_VERSION
#define TAWNI_VERSION "0.0.0"
#endif

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  Serial.printf("Tawni Wardriver v%s — firmware TBD\n", TAWNI_VERSION);
}

void loop() {}
