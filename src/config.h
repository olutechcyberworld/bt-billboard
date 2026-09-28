#pragma once
#include <Arduino.h>

// ── Single source of truth for the firmware version ─────────────────────────
#define FW_VERSION  "3.4.0"

// ── Hardware ────────────────────────────────────────────────────────────────
#define NUM_DEVICES         4
#define DISPLAY_COLS        (NUM_DEVICES * 8)

#define PIN_DATA            6
#define PIN_CLK             7
#define PIN_CS              10
#define PIN_SDA             4
#define PIN_SCL             5
#define PIN_BUZZER          3    // GPIO3: not a strapping pin on C3, safe post-boot

// ── Networking ──────────────────────────────────────────────────────────────
// DEVICE_HOSTNAME_PREFIX / PROVISIONING_AP_PREFIX: the actual mDNS hostname
// and captive-portal AP name are these prefixes plus a 6-hex-char suffix
// derived from this unit's factory MAC address (see transport.cpp), so that
// multiple billboards on the same network never collide on either name.
#define DEVICE_HOSTNAME_PREFIX     "bt-billboard"
#define PROVISIONING_AP_PREFIX     "BT-Billboard-Setup"
#define WS_PORT             81

// ── Display / protocol limits ───────────────────────────────────────────────
#define MAX_MSG_LEN         256
#define NO_MESSAGE_PLACEHOLDER "No message set"
#define SCROLL_PASSES       3
#define SCROLL_SPACING      3

#define RTC_REFRESH_MS      1000UL
#define CLOCK_DATE_SWITCH_MS 3000UL

#define SPEED_DELAY_MAX     80U
#define SPEED_DELAY_MIN     10U
#define SPEED_DEFAULT_PCT   50

#define BRIGHTNESS_DEFAULT_PCT 25
#define BRIGHTNESS_MAX_INTENS  15U

#define PAROLA_ANIM_COUNT   27
#define ANIM_COMPOSITE_CURSOR 27
#define ANIM_COMPOSITE_SCANH  28
#define ANIM_COUNT          29

// 8-wide font + 2 spacing = 10 bytes per char worst case
#define SHADOW_BUF_MAX      (MAX_MSG_LEN * 10)
#define CURSOR_BLINK_MS     300UL
#define SCAN_STEP_MS        4UL

// ── Scheduler ───────────────────────────────────────────────────────────────
#define MAX_SCHEDULES       16
#define SCHED_MSG_LEN       48
#define ALARM_DEFAULT_SEC   30