#include "buzzer.h"
#include "config.h"

// Non-blocking pattern player. Each pattern is a sequence of {onMs, offMs}
// steps terminated by {0, 0}. Playback advances purely from buzzer_tick(),
// so nothing here ever blocks the main loop.

struct BuzzStep { uint16_t onMs; uint16_t offMs; };

static const BuzzStep PAT_SHORT[]      = { {120, 0},  {0,0} };
static const BuzzStep PAT_DOUBLE[]     = { {100, 90}, {100, 0}, {0,0} };
static const BuzzStep PAT_TRIPLE[]     = { { 90, 80}, { 90, 80}, { 90, 0}, {0,0} };
static const BuzzStep PAT_LONG[]       = { {900, 0},  {0,0} };
static const BuzzStep PAT_CONTINUOUS[] = { {60000, 300}, {60000, 300}, {60000, 300},
                                           {60000, 300}, {60000, 300}, {60000, 300},
                                           {60000, 300}, {60000, 300}, {60000, 300},
                                           {60000, 300}, {60000, 300}, {60000, 300},
                                           {60000, 300}, {60000, 300}, {60000, 300},
                                           {60000, 300}, {60000, 300}, {60000, 300},
                                           {60000, 300}, {60000, 300}, {0,0} };

static const BuzzStep* const PATTERNS[] = {
  PAT_SHORT, PAT_DOUBLE, PAT_TRIPLE, PAT_LONG, PAT_CONTINUOUS
};

static const BuzzStep* gPat       = nullptr;
static uint8_t         gStep      = 0;
static bool            gToneOn    = false;
static unsigned long   gStepTS    = 0;

#define BUZZ_FREQ 2700   // passive piezo resonant frequency

void buzzer_begin() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  Serial.printf("[BUZZ] Ready on GPIO%d\n", PIN_BUZZER);
}

void buzzer_play(BuzzPattern p) {
  if (p > BUZZ_CONTINUOUS) return;
  gPat    = PATTERNS[p];
  gStep   = 0;
  gToneOn = false;
  gStepTS = millis();
  Serial.printf("[BUZZ] Pattern %u\n", p);
}

void buzzer_stop() {
  gPat = nullptr;
  gToneOn = false;
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);
}

bool buzzer_is_active() { return gPat != nullptr; }

void buzzer_tick() {
  if (!gPat) return;
  const BuzzStep& s = gPat[gStep];

  if (s.onMs == 0 && s.offMs == 0) {   // end marker
    buzzer_stop();
    return;
  }

  unsigned long now = millis();
  unsigned long elapsed = now - gStepTS;

  if (gToneOn) {
    if (elapsed >= s.onMs) {
      noTone(PIN_BUZZER);
      digitalWrite(PIN_BUZZER, LOW);
      gToneOn = false;
      gStepTS = now;
    }
  } else {
    if (elapsed >= s.offMs) {
      tone(PIN_BUZZER, BUZZ_FREQ);
      gToneOn = true;
      gStepTS = now;
      gStep++;
    }
  }
}