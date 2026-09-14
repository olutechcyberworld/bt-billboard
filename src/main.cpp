/**
 * ============================================================================
 * BT Billboard — ESP32-C3 Firmware v3.4.0
 * ============================================================================
 * Hardware   : ESP32-C3 Super Mini
 *              FC-16 4-unit MAX7219 LED Dot Matrix  (32 × 8)
 *              DS3231 RTC
 *              Passive piezo buzzer on GPIO3
 *
 * Pin map    : see config.h
 * Protocol   : see protocol.cpp
 * ============================================================================
 */

#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "clock.h"
#include "buzzer.h"
#include "scheduler.h"
#include "transport.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[BOOT] BT Billboard v%s\n", FW_VERSION);

  display_begin();
  clock_begin();
  buzzer_begin();
  scheduler_begin();

  // transport_begin() drives the display itself while it waits for Wi-Fi;
  // it returns only once the WebSocket server and OTA are live.
  transport_begin();

  display_start_boot_marquee();
}

void loop() {
  transport_loop();
  display_tick();
  scheduler_tick();
  buzzer_tick();
}