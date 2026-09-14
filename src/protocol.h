#pragma once
#include <Arduino.h>

void protocol_dispatch(uint8_t clientNum, const String& frame);