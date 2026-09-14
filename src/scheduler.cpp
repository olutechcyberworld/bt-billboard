#include "scheduler.h"
#include "config.h"
#include "clock.h"
#include "display.h"
#include "buzzer.h"
#include <Preferences.h>
#include <string.h>

static ScheduleEntry gEntries[MAX_SCHEDULES];
static Preferences   gPrefs;
static uint16_t      gLastMinute = 0xFFFF;   // minute-of-day of last scan

#define NVS_NAMESPACE "bbsched"
#define NVS_KEY       "entries"

static void load() {
  gPrefs.begin(NVS_NAMESPACE, true);
  size_t n = gPrefs.getBytesLength(NVS_KEY);
  if (n == sizeof(gEntries)) gPrefs.getBytes(NVS_KEY, gEntries, n);
  else                        memset(gEntries, 0, sizeof(gEntries));
  gPrefs.end();
}

static void save() {
  gPrefs.begin(NVS_NAMESPACE, false);
  gPrefs.putBytes(NVS_KEY, gEntries, sizeof(gEntries));
  gPrefs.end();
}

void scheduler_begin() {
  load();
  uint8_t active = 0;
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++)
    if (gEntries[i].enabled) active++;
  Serial.printf("[SCHED] Loaded %u active schedule(s)\n", active);
}

bool scheduler_set(uint8_t idx, const ScheduleEntry& e) {
  if (idx >= MAX_SCHEDULES) return false;
  gEntries[idx] = e;
  save();
  return true;
}

bool scheduler_delete(uint8_t idx) {
  if (idx >= MAX_SCHEDULES) return false;
  memset(&gEntries[idx], 0, sizeof(ScheduleEntry));
  save();
  return true;
}

void scheduler_clear() {
  memset(gEntries, 0, sizeof(gEntries));
  save();
}

const ScheduleEntry* scheduler_get(uint8_t idx) {
  if (idx >= MAX_SCHEDULES) return nullptr;
  return &gEntries[idx];
}

uint8_t scheduler_count() { return MAX_SCHEDULES; }

// ─────────────────────────────────────────────────────────────────────────────
void scheduler_tick() {
  // Gate on a valid RTC: an unset DS3231 reads 2000-01-01 and would fire
  // every midnight schedule the instant the device boots.
  if (!clock_is_valid()) return;

  DateTime now = clock_now();
  uint16_t minuteOfDay = (uint16_t)now.hour() * 60 + now.minute();
  if (minuteOfDay == gLastMinute) return;      // already scanned this minute
  gLastMinute = minuteOfDay;

  uint8_t dow = now.dayOfTheWeek();

  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    ScheduleEntry& e = gEntries[i];
    if (!e.enabled) continue;
    if (e.hour != now.hour() || e.minute != now.minute()) continue;
    if (!(e.daysMask & (1 << dow))) continue;

    Serial.printf("[SCHED] Fire #%u: %s\n", i, e.message);

    if (e.action == ACT_DISPLAY || e.action == ACT_BOTH) {
      display_push_alarm(e.message, e.durationSec);
    }
    if (e.action == ACT_BUZZ || e.action == ACT_BOTH) {
      buzzer_play((BuzzPattern)e.buzzPattern);
    }
  }
}