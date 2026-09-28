#include "buzzer.h"
#include "config.h"

// Non-blocking pattern player. Each pattern is a sequence of
// {onMs, offMs, freq} steps terminated by {0, 0, 0}. Playback advances
// purely from buzzer_tick(), so nothing here ever blocks the main loop.
//
// Every step carries its own frequency (previously all steps shared one
// fixed BUZZ_FREQ_REF, so patterns were distinguishable only by timing --
// hard to perceive on a small piezo). Frequencies here stay within roughly
// +-20% of BUZZ_FREQ_REF, the documented resonant frequency for this
// buzzer, since a passive piezo's output volume can drop off sharply
// outside its resonant band: going further for more dramatic pitch
// contrast risks making a pattern noticeably quieter on this specific
// piece of hardware rather than just differently pitched. If a particular
// pattern still sounds too quiet on your unit, that's the number to tune.

struct BuzzStep { uint16_t onMs; uint16_t offMs; uint16_t freq; };

#define BUZZ_FREQ_REF 2700   // passive piezo resonant frequency

static const BuzzStep PAT_SHORT[]  = {
  {120, 0, BUZZ_FREQ_REF},
  {0, 0, 0}
};
static const BuzzStep PAT_DOUBLE[] = {
  {100, 90, 3100}, {100, 0, 3100},
  {0, 0, 0}
};
static const BuzzStep PAT_TRIPLE[] = {
  {90, 80, 2200}, {90, 80, 2200}, {90, 0, 2200},
  {0, 0, 0}
};
// A flat 900ms tone is easy to mistake for a long, held version of SHORT.
// A two-stage warble (rising) makes "sustained" unmistakable at a glance
// -- er, a listen -- without depending on duration alone to carry it.
static const BuzzStep PAT_LONG[] = {
  {450, 0, 2500}, {450, 0, 3200},
  {0, 0, 0}
};
// Alternating two-tone siren, distinct from LONG's one-time warble by
// repeating for as long as the alarm stays active.
static const BuzzStep PAT_CONTINUOUS[] = {
  {600, 200, 2500}, {600, 200, 3200},
  {600, 200, 2500}, {600, 200, 3200},
  {600, 200, 2500}, {600, 200, 3200},
  {600, 200, 2500}, {600, 200, 3200},
  {600, 200, 2500}, {600, 200, 3200},
  {600, 200, 2500}, {600, 200, 3200},
  {600, 200, 2500}, {600, 200, 3200},
  {600, 200, 2500}, {600, 200, 3200},
  {600, 200, 2500}, {600, 200, 3200},
  {600, 200, 2500}, {600, 200, 3200},
  {0, 0, 0}
};

static const BuzzStep* const PATTERNS[] = {
  PAT_SHORT, PAT_DOUBLE, PAT_TRIPLE, PAT_LONG, PAT_CONTINUOUS
};

static const BuzzStep* gPat       = nullptr;
static uint8_t         gStep      = 0;
static bool            gToneOn    = false;
static unsigned long   gStepTS    = 0;

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
      gStep++;
    }
  } else {
    if (elapsed >= s.offMs) {
      tone(PIN_BUZZER, s.freq);
      gToneOn = true;
      gStepTS = now;
    }
  }
}