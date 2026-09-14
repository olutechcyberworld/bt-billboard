#pragma once
#include <Arduino.h>

void transport_begin();                                  // provisioning + WS + OTA + mDNS
void transport_loop();                                   // pump WS + OTA

void transport_reply(uint8_t clientNum, const char* frame);
void transport_broadcast(const char* frame);