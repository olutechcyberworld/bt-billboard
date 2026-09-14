#pragma once
#include <Arduino.h>

struct FontEntry {
  const char*    name;
  const uint8_t* data;   // PROGMEM font blob, or nullptr for the library default
};

extern const FontEntry FONT_REGISTRY[];
extern const uint8_t   FONT_REGISTRY_COUNT;