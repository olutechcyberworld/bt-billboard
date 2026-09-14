#pragma once
#include <Arduino.h>
#include "types.h"

void buzzer_begin();
void buzzer_play(BuzzPattern p);
void buzzer_stop();
void buzzer_tick();
bool buzzer_is_active();