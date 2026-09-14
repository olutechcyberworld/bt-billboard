#include "clock.h"
#include "config.h"

static RTC_DS3231 rtc;
static bool       gPresent = false;

static const char* const WEEKDAY_NAMES[7] = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char* const MONTH_NAMES[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

bool clock_begin() {
  gPresent = rtc.begin();
  if (!gPresent) {
    Serial.println(F("[RTC]  ERROR: DS3231 not detected."));
    return false;
  }
  if (rtc.lostPower()) {
    Serial.println(F("[RTC]  WARNING: power lost, awaiting sync."));
  } else {
    Serial.println(F("[RTC]  OK"));
  }
  return true;
}

bool clock_is_valid() {
  if (!gPresent) return false;
  // Guard against a chip that is present but still holding its 2000-01-01
  // power-on default: without this, the scheduler would fire any midnight
  // schedule and the clock display would render a bogus year.
  return rtc.now().year() >= 2024;
}

DateTime clock_now() { return rtc.now(); }

bool clock_set(uint16_t y, uint8_t mo, uint8_t d,
               uint8_t h, uint8_t mi, uint8_t s) {
  if (!gPresent) return false;
  rtc.adjust(DateTime(y, mo, d, h, mi, s));
  return true;
}

String clock_time_str() {
  DateTime now = rtc.now();
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", now.hour(), now.minute());
  return String(buf);
}

String clock_named_datetime() {
  DateTime now = rtc.now();
  char buf[40];
  snprintf(buf, sizeof(buf), "%s, %s %d  %02d:%02d",
           WEEKDAY_NAMES[now.dayOfTheWeek()],
           MONTH_NAMES[now.month() - 1],
           now.day(), now.hour(), now.minute());
  return String(buf);
}

void clock_numeric_date(DateFormat f, char* out, size_t outSize) {
  DateTime now = rtc.now();
  switch (f) {
    case FMT_DD_SLASH_MM:
      snprintf(out, outSize, "%02d/%02d", now.day(), now.month());
      break;
    case FMT_MM_DD:
      snprintf(out, outSize, "%02d-%02d", now.month(), now.day());
      break;
    case FMT_DD_MM:
    default:
      snprintf(out, outSize, "%02d-%02d", now.day(), now.month());
      break;
  }
}