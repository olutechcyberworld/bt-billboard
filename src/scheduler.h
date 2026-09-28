#pragma once
#include <Arduino.h>
#include "types.h"
#include "config.h"   // SCHED_MSG_LEN — this header must be self-contained,
                       // not rely on whichever .cpp happens to include
                       // config.h before scheduler.h

struct ScheduleEntry {
  uint8_t enabled;
  uint8_t hour;
  uint8_t minute;
  uint8_t daysMask;      // bit0=Sun .. bit6=Sat
  uint8_t action;        // AlarmAction
  uint8_t buzzPattern;   // BuzzPattern
  uint8_t durationSec;   // 0 = ALARM_DEFAULT_SEC
  uint8_t reserved;
  char    message[SCHED_MSG_LEN];
};

void scheduler_begin();
void scheduler_tick();

bool scheduler_set(uint8_t idx, const ScheduleEntry& e);
bool scheduler_delete(uint8_t idx);
void scheduler_clear();

const ScheduleEntry* scheduler_get(uint8_t idx);
uint8_t scheduler_count();