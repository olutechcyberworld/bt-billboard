#pragma once
#include <Arduino.h>
#include <RTClib.h>
#include "types.h"

bool     clock_begin();
bool     clock_is_valid();
DateTime clock_now();
bool     clock_set(uint16_t y, uint8_t mo, uint8_t d,
                   uint8_t h, uint8_t mi, uint8_t s);

String   clock_time_str();                       // "HH:MM"
String   clock_named_datetime();                 // "Mon, Sep 10  14:32"
void     clock_numeric_date(DateFormat f, char* out, size_t outSize);