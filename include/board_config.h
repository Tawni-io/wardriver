#pragma once
/***********************config*************************/
#define IIC_SCL_PIN 3
#define IIC_SDA_PIN 2

#define LCD_WIDTH 170
#define LCD_HEIGHT 320

#define BUTTON_PIN 0
#define BUTTON_BOOT 28

#ifndef USE_TOUCH_SWIPE
#define USE_TOUCH_SWIPE 0
#endif

#if USE_TOUCH_SWIPE
#define TP_INT 27
#define TP_RST 24
#endif

#define AXP2602_INT 10

#define LCD_RST 23
#define LCD_SCK 7
#define LCD_CS 26
#define LCD_DC 8
#define LCD_MOSI 9
#define LCD_BLK_POWER 25

// Rufous GPS — Seeed XIAO L76K GNSS add-on on right header silk TXD/RXD.
// Crossed UART: module TX (D7) → board RXD, module RX (D6) → board TXD.
#define GPS_UART_TX 11  // ESP TX → GPS RX (header TXD)
#define GPS_UART_RX 12  // ESP RX ← GPS TX (header RXD)
#define GPS_UART_BAUD 9600
// L76K WAKEUP (WUP / D0): HIGH = run, LOW = standby. LP GPIO — hold through deep sleep.
#define GPS_WAKEUP_PIN 1
