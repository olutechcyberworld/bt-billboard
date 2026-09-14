#pragma once
#include <Arduino.h>

enum DisplayState : uint8_t {
  STATE_BOOT,
  STATE_PROVISION,
  STATE_RUNNING,
  STATE_ALARM
};

enum DisplayMode : uint8_t {
  MODE_MESSAGE    = 0,
  MODE_CLOCK      = 1,
  MODE_CLOCK_DATE = 2
};

enum DateFormat : uint8_t {
  FMT_NAMED       = 0,
  FMT_DD_MM       = 1,
  FMT_DD_SLASH_MM = 2,
  FMT_MM_DD       = 3
};

enum BuzzPattern : uint8_t {
  BUZZ_SHORT      = 0,
  BUZZ_DOUBLE     = 1,
  BUZZ_TRIPLE     = 2,
  BUZZ_LONG       = 3,
  BUZZ_CONTINUOUS = 4
};

enum AlarmAction : uint8_t {
  ACT_DISPLAY = 0,
  ACT_BUZZ    = 1,
  ACT_BOTH    = 2
};