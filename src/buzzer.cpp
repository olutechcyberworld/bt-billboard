#include "buzzer.h"
#include "config.h"

// Non-blocking pattern player. Each pattern is a sequence of {onMs, offMs}
// steps terminated by {0, 0}. Playback advances purely from buzzer_tick(),
// so nothing here ever blocks the main loop.
//
// This drives the buzzer with plain digitalWrite(HIGH/LOW), not tone().
// This is an ACTIVE buzzer -- it has its own internal oscillator and makes
// its one fixed tone whenever powered, so there is no real "frequency" to
// tune here. tone() was previously used to vary pitch per pattern for
// audible distinction, but on an active buzzer that just chops its fixed
// internal tone on and off at whatever rate tone() was given; at some
// chop rates that comes out quieter or muddier than a solid drive, which
// is almost certainly why patterns using it sounded too low. Driving the
// pin directly guarantees full, undiminished volume on every pulse.
// Patterns are told apart by rhythm alone: pulse count, pulse length, and
// gap length, not pitch.

struct BuzzStep { uint16_t onMs; uint16_t offMs; };

static const BuzzStep PAT_SHORT[] = {
  {120, 0},
  {0, 0}
};
// Two clean, evenly-spaced pulses -- a deliberate, measured "beep-beep".
static const BuzzStep PAT_DOUBLE[] = {
  {120, 100}, {120, 0},
  {0, 0}
};
// Three quicker, tighter pulses -- a different cadence from DOUBLE, not
// just "one more of the same pulse", so the two don't blur into each other
// by pulse count alone.
static const BuzzStep PAT_TRIPLE[] = {
  {90, 70}, {90, 70}, {90, 0},
  {0, 0}
};
// One solid, continuous drone. Texturally distinct from every pulsed
// pattern above by virtue of never turning off mid-pattern.
static const BuzzStep PAT_LONG[] = {
  {900, 0},
  {0, 0}
};
// A repeating long-buzz-then-pause siren cadence -- distinct from LONG
// (which is a one-shot drone) by continuing to cycle for as long as the
// alarm stays active.
static const BuzzStep PAT_CONTINUOUS[] = {
  {700, 300}, {700, 300}, {700, 300}, {700, 300}, {700, 300},
  {700, 300}, {700, 300}, {700, 300}, {700, 300}, {700, 300},
  {700, 300}, {700, 300}, {700, 300}, {700, 300}, {700, 300},
  {0, 0}
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
      digitalWrite(PIN_BUZZER, LOW);
      gToneOn = false;
      gStepTS = now;
      gStep++;
    }
  } else {
    if (elapsed >= s.offMs) {
      digitalWrite(PIN_BUZZER, HIGH);
      gToneOn = true;
      gStepTS = now;
    }
  }
}