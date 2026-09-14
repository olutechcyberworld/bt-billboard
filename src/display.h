#pragma once
#include <Arduino.h>
#include "types.h"

void display_begin();
void display_tick();

// Content setters
void display_set_message(const char* msg, bool* accepted);
void display_clear();

// Configuration
void display_set_mode(DisplayMode m);
void display_set_date_format(DateFormat f);
bool display_set_anim(uint8_t idx);
void display_set_speed(uint8_t pct);
void display_set_brightness(uint8_t pct);
bool display_set_font(uint8_t id);

// Boot / provisioning
void display_start_boot_marquee();
void display_show_provisioning(const char* msg);
void display_end_provisioning();

// Alarm overlay
bool display_push_alarm(const char* msg, uint16_t durationSec);
void display_dismiss_alarm();
bool display_is_alarm_active();

// Introspection
DisplayMode display_get_mode();
DateFormat  display_get_date_format();
uint8_t     display_get_font();
bool        display_is_booting();

// Callback the protocol layer installs so D: pass-complete notifications
// can be broadcast without display.cpp including transport/protocol.
typedef void (*DisplayPassCb)(void);
void display_set_pass_callback(DisplayPassCb cb);