#pragma once
#include <Arduino.h>

void transport_begin();                                  // provisioning + WS + OTA + mDNS
void transport_loop();                                   // pump WS + OTA

void transport_reply(uint8_t clientNum, const char* frame);
void transport_broadcast(const char* frame);

// Per-device identity, derived from the factory STA MAC address so multiple
// units on the same network never collide on hostname or AP name. Valid
// only after transport_begin() has run.
const char* transport_device_id();   // 6 lowercase hex chars, e.g. "a1b2c3"
const char* transport_hostname();    // e.g. "bt-billboard-a1b2c3"
const char* transport_mac();         // "AA:BB:CC:DD:EE:FF"