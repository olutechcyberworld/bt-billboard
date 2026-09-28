#include "display.h"
#include "config.h"
#include "clock.h"
#include "fonts.h"
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <string.h>

// ── Hardware instance ───────────────────────────────────────────────────────
static MD_Parola P(MD_MAX72XX::FC16_HW, PIN_DATA, PIN_CLK, PIN_CS, NUM_DEVICES);

// ── Animation preset table (Parola effects only, 0..26) ─────────────────────
struct AnimPreset {
  const char*    name;
  textPosition_t align;
  textEffect_t   effectIn;
  textEffect_t   effectOut;
  uint16_t       pause;
};

static const AnimPreset ANIM_PRESETS[PAROLA_ANIM_COUNT] = {
  { "Scroll Left",     PA_LEFT,   PA_SCROLL_LEFT,       PA_SCROLL_LEFT,       0    },
  { "Scroll Right",    PA_LEFT,   PA_SCROLL_RIGHT,      PA_SCROLL_RIGHT,      0    },
  { "Scroll Up",       PA_CENTER, PA_SCROLL_UP,         PA_SCROLL_UP,         0    },
  { "Scroll Down",     PA_CENTER, PA_SCROLL_DOWN,       PA_SCROLL_DOWN,       0    },
  { "Scroll Up-Left",  PA_LEFT,   PA_SCROLL_UP_LEFT,    PA_SCROLL_UP_LEFT,    0    },
  { "Scroll Up-Right", PA_LEFT,   PA_SCROLL_UP_RIGHT,   PA_SCROLL_UP_RIGHT,   0    },
  { "Scroll Dn-Left",  PA_LEFT,   PA_SCROLL_DOWN_LEFT,  PA_SCROLL_DOWN_LEFT,  0    },
  { "Scroll Dn-Right", PA_LEFT,   PA_SCROLL_DOWN_RIGHT, PA_SCROLL_DOWN_RIGHT, 0    },
  { "Fade",            PA_CENTER, PA_FADE,              PA_FADE,              2000 },
  { "Blinds",          PA_CENTER, PA_BLINDS,            PA_BLINDS,            2000 },
  { "Dissolve",        PA_CENTER, PA_DISSOLVE,          PA_DISSOLVE,          2000 },
  { "Mesh",            PA_CENTER, PA_MESH,              PA_MESH,              2000 },
  { "Slice",           PA_CENTER, PA_SLICE,             PA_SLICE,             2000 },
  { "Wipe",            PA_CENTER, PA_WIPE,              PA_WIPE,              2000 },
  { "Wipe Cursor",     PA_CENTER, PA_WIPE_CURSOR,       PA_WIPE_CURSOR,       2000 },
  { "Scan H",          PA_CENTER, PA_SCAN_HORIZ,        PA_SCAN_HORIZ,        2000 },
  { "Scan H Alt",      PA_CENTER, PA_SCAN_HORIZX,       PA_SCAN_HORIZX,       2000 },
  { "Scan V",          PA_CENTER, PA_SCAN_VERT,         PA_SCAN_VERT,         2000 },
  { "Scan V Alt",      PA_CENTER, PA_SCAN_VERTX,        PA_SCAN_VERTX,        2000 },
  { "Opening",         PA_CENTER, PA_OPENING,           PA_CLOSING,           2000 },
  { "Opening Cursor",  PA_CENTER, PA_OPENING_CURSOR,    PA_CLOSING_CURSOR,    2000 },
  { "Closing",         PA_CENTER, PA_CLOSING,           PA_OPENING,           2000 },
  { "Closing Cursor",  PA_CENTER, PA_CLOSING_CURSOR,    PA_OPENING_CURSOR,    2000 },
  { "Grow Up",         PA_CENTER, PA_GROW_UP,           PA_GROW_DOWN,         2000 },
  { "Grow Down",       PA_CENTER, PA_GROW_DOWN,         PA_GROW_UP,           2000 },
  { "Instant",         PA_CENTER, PA_PRINT,             PA_NO_EFFECT,         3000 },
  { "Random",          PA_CENTER, PA_RANDOM,            PA_RANDOM,            2000 },
};

// ── Boot marquee ────────────────────────────────────────────────────────────
static const char* BOOT_MSGS[] = {
  "Booting...",
  "BT-Billboard v" FW_VERSION,
  "Engineered by Olutech Cyberworld",
  "For further enquiries, contact us @ 07015594518, 09151745535"
};
static const uint8_t BOOT_MSG_COUNT =
    (uint8_t)(sizeof(BOOT_MSGS) / sizeof(BOOT_MSGS[0]));

// ── Module state ────────────────────────────────────────────────────────────
static DisplayState gState     = STATE_BOOT;
static DisplayMode  gMode      = MODE_MESSAGE;
static DateFormat   gFmt       = FMT_NAMED;
static uint8_t      gAnim      = 0;
static uint8_t      gFont      = 0;
static uint8_t      gSpeed     = (uint8_t)(SPEED_DELAY_MAX -
                       ((uint32_t)SPEED_DEFAULT_PCT *
                       (SPEED_DELAY_MAX - SPEED_DELAY_MIN)) / 100UL);
static uint8_t      gBright    = BRIGHTNESS_DEFAULT_PCT;
static uint8_t      gBootIdx   = 0;
static bool         gDeferClear = false;

// Distinct buffers — the user message and the clock/date string never share
// storage again (fixes A4/B5 from the review).
static char gMsgBuf   [MAX_MSG_LEN + 1] = { '\0' };
static char gQueueBuf [MAX_MSG_LEN + 1] = { '\0' };
static bool gQueueFull = false;
static bool gShowingPlaceholder = false;   // gMsgBuf holds NO_MESSAGE_PLACEHOLDER, not real content
static char gClockBuf [MAX_MSG_LEN + 1] = { '\0' };
static char gAlarmBuf [MAX_MSG_LEN + 1] = { '\0' };
static char gLastClockStr[8] = { '\0' };

// Scroll bookkeeping
static uint8_t gPass = 0;
static bool    gComposite = false;
static unsigned long gClockAltTS = 0;
static bool    gClockShowTime = true;

// Provisioning / alarm overlays
static bool     gProvisioning = false;
static unsigned long gAlarmUntil = 0;
static DisplayMode   gSavedMode = MODE_MESSAGE;
static DateFormat    gSavedFmt  = FMT_NAMED;
static char          gSavedMsg [MAX_MSG_LEN + 1] = { '\0' };

// Composite effect state
static uint8_t  shadowBuf[SHADOW_BUF_MAX];
static uint16_t shadowWidth    = 0;
static int32_t  compScrollX    = 0;
static unsigned long compScrollTS = 0;
static unsigned long compFxTS     = 0;
static bool     cursorVisible  = false;
static uint8_t  scanRow        = 0;

// Pass-complete callback (installed by protocol.cpp)
static DisplayPassCb gPassCb = nullptr;

// ── Forward decls ───────────────────────────────────────────────────────────
static void  apply_anim();
static void  restart_scroll();
static void  on_pass_complete();
static void  promote_queue();
static void  build_shadow_buf(const char* msg);
static void  composite_tick();
static void  clock_refresh();
static void  clock_date_repaint();
static void  clock_date_static_tick();
static const char* active_scroll_text();
static bool  scrolling_active();
static void  blank_hardware();

// ─────────────────────────────────────────────────────────────────────────────
void display_begin() {
  P.begin();
  P.setIntensity((uint8_t)((uint32_t)BRIGHTNESS_DEFAULT_PCT *
                           BRIGHTNESS_MAX_INTENS / 100UL));
  P.setScrollSpacing(SCROLL_SPACING);
  Serial.println(F("[DISP] Parola initialised."));
}

void display_set_pass_callback(DisplayPassCb cb) { gPassCb = cb; }

// ─────────────────────────────────────────────────────────────────────────────
// Boot sequence
// ─────────────────────────────────────────────────────────────────────────────
void display_start_boot_marquee() {
  gState   = STATE_BOOT;
  gBootIdx = 0;
  P.displayText(BOOT_MSGS[0], PA_LEFT, gSpeed, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
}

// ─────────────────────────────────────────────────────────────────────────────
// Provisioning overlay — driven entirely by transport.cpp while WiFiManager
// is either blocking (brief) or spinning in its non-blocking portal loop.
//
// display.cpp keeps its own copy of the text (gProvisionBuf) rather than
// holding onto the caller's pointer: callers pass local/stack buffers (see
// transport.cpp's apMsg/ipMsg), and MD_Parola::displayText() only stores the
// pointer it's given, not a copy, so display_tick() re-issuing that same
// pointer on every animation-complete tick needs it to still be valid for as
// long as the overlay is showing. Today's call sites happen to keep their
// buffer in scope for the whole time display_tick() is looping (transport.cpp
// doesn't return until the overlay is done), but that is a fragile invariant
// to depend on, not a guarantee — and MD_Parola has no accessor to read the
// text back out, so the tick loop cannot ask the library for it either.
static char gProvisionBuf[128];

void display_show_provisioning(const char* msg) {
  gState        = STATE_PROVISION;
  gProvisioning = true;
  strncpy(gProvisionBuf, msg, sizeof(gProvisionBuf) - 1);
  gProvisionBuf[sizeof(gProvisionBuf) - 1] = '\0';
  P.displayClear();
  P.displayText(gProvisionBuf, PA_LEFT, gSpeed, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
}

void display_end_provisioning() { gProvisioning = false; }

// ─────────────────────────────────────────────────────────────────────────────
// Content setters
// ─────────────────────────────────────────────────────────────────────────────
void display_set_message(const char* msg, bool* accepted) {
  *accepted = false;
  if (!msg || !msg[0]) return;

  if (gState == STATE_BOOT || gState == STATE_PROVISION) {
    if (!gQueueFull) {
      strncpy(gQueueBuf, msg, MAX_MSG_LEN);
      gQueueBuf[MAX_MSG_LEN] = '\0';
      gQueueFull = true;
      *accepted  = true;          // queued for after boot — tell the client now
    }
    return;
  }

  if (gState == STATE_ALARM) {
    // A write into gMsgBuf here would be invisible (active_scroll_text()
    // returns gAlarmBuf while alarming) and would then be clobbered when
    // display_dismiss_alarm() restores gMsgBuf from gSavedMsg. Queue it
    // instead so it surfaces once the alarm clears.
    if (!gQueueFull) {
      strncpy(gQueueBuf, msg, MAX_MSG_LEN);
      gQueueBuf[MAX_MSG_LEN] = '\0';
      gQueueFull = true;
      *accepted  = true;          // queued for after the alarm — tell the client now
    }
    return;
  }

  bool busy = (gMode == MODE_MESSAGE) && (gMsgBuf[0] != '\0') && !gShowingPlaceholder;
  gMode = MODE_MESSAGE;

  if (!busy) {
    strncpy(gMsgBuf, msg, MAX_MSG_LEN);
    gMsgBuf[MAX_MSG_LEN] = '\0';
    gQueueFull = false;
    gShowingPlaceholder = false;
    restart_scroll();
    *accepted = true;
  } else if (!gQueueFull) {
    strncpy(gQueueBuf, msg, MAX_MSG_LEN);
    gQueueBuf[MAX_MSG_LEN] = '\0';
    gQueueFull = true;
  }
}

void display_clear() {
  gMsgBuf[0]   = '\0';
  gQueueBuf[0] = '\0';
  gQueueFull   = false;
  gShowingPlaceholder = false;

  if (gState == STATE_BOOT || gState == STATE_PROVISION) {
    gDeferClear = true;
    return;
  }
  if (gState == STATE_ALARM) {
    display_dismiss_alarm();
    return;
  }
  gMode = MODE_MESSAGE;
  blank_hardware();                // fixes A1
}

// Physical blank, not just a state reset.
static void blank_hardware() {
  P.displayClear();                // stops any in-progress effect
  MD_MAX72XX* mx = P.getGraphicObject();
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  for (uint8_t c = 0; c < DISPLAY_COLS; c++) mx->setColumn(c, 0x00);
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
  gComposite = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mode / config
// ─────────────────────────────────────────────────────────────────────────────
void display_set_mode(DisplayMode m) {
  gMode = m;
  if (m == MODE_MESSAGE) {
    // Fix B7: pull a queued message forward if the slot is free.
    if (gMsgBuf[0] == '\0' && gQueueFull) promote_queue();
    if (gMsgBuf[0] != '\0') {
      restart_scroll();
    } else {
      // Nothing set yet: a dark panel reads as "device is off/broken", not
      // "waiting for input". Show a placeholder instead. gShowingPlaceholder
      // tracks that this is not real content — see display_set_message(),
      // which must not treat a placeholder as an occupied slot.
      strncpy(gMsgBuf, NO_MESSAGE_PLACEHOLDER, MAX_MSG_LEN);
      gMsgBuf[MAX_MSG_LEN] = '\0';
      gShowingPlaceholder = true;
      restart_scroll();
    }
  } else if (m == MODE_CLOCK) {
    gLastClockStr[0] = '\0';       // force repaint
    clock_refresh();
  } else if (m == MODE_CLOCK_DATE) {
    if (gFmt == FMT_NAMED) {
      strncpy(gClockBuf, clock_named_datetime().c_str(), MAX_MSG_LEN);
      gClockBuf[MAX_MSG_LEN] = '\0';
      restart_scroll();
    } else {
      gClockShowTime = true;
      gClockAltTS    = millis();
      clock_date_repaint();
    }
  }
}

void display_set_date_format(DateFormat f) {
  gFmt = f;
  if (gMode == MODE_CLOCK_DATE) display_set_mode(MODE_CLOCK_DATE);
}

bool display_set_anim(uint8_t idx) {
  if (idx >= ANIM_COUNT) return false;
  gAnim = idx;
  if (scrolling_active()) restart_scroll();
  return true;
}

void display_set_speed(uint8_t pct) {
  if (pct > 100) pct = 100;
  gSpeed = (uint8_t)(SPEED_DELAY_MAX -
           ((uint32_t)pct * (SPEED_DELAY_MAX - SPEED_DELAY_MIN)) / 100UL);
  P.setSpeed(gSpeed);
  // Fix B3: Parola latches speed at displayText() time, so a live change only
  // takes effect on the next restart.
  if (scrolling_active()) restart_scroll();
}

void display_set_brightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  gBright = pct;
  P.setIntensity((uint8_t)((uint32_t)pct * BRIGHTNESS_MAX_INTENS / 100UL));
}

bool display_set_font(uint8_t id) {
  if (id >= FONT_REGISTRY_COUNT) return false;
  gFont = id;
  if (FONT_REGISTRY[id].data) {
    P.getGraphicObject()->setFont((uint8_t*)FONT_REGISTRY[id].data);
  }
  // The library default cannot be re-selected without a reboot; slot 0's
  // nullptr means "leave whatever is currently loaded alone". Documented
  // limitation, not a silent bug.
  if (scrolling_active()) restart_scroll();
  return true;
}

DisplayMode display_get_mode()        { return gMode; }
DateFormat  display_get_date_format() { return gFmt;  }
uint8_t     display_get_font()        { return gFont; }
bool        display_is_booting() {
  return gState == STATE_BOOT || gState == STATE_PROVISION;
}

// ─────────────────────────────────────────────────────────────────────────────
// Alarm overlay
// ─────────────────────────────────────────────────────────────────────────────
static bool gSavedWasPlaceholder = false;

bool display_push_alarm(const char* msg, uint16_t durationSec) {
  if (!msg || !msg[0]) return false;
  if (gState == STATE_ALARM) return false;      // already showing one

  gSavedMode = gMode;
  gSavedFmt  = gFmt;
  gSavedWasPlaceholder = gShowingPlaceholder;
  strncpy(gSavedMsg, gMsgBuf, MAX_MSG_LEN); gSavedMsg[MAX_MSG_LEN] = '\0';

  strncpy(gAlarmBuf, msg, MAX_MSG_LEN);     gAlarmBuf[MAX_MSG_LEN] = '\0';
  gState      = STATE_ALARM;
  gAlarmUntil = millis() + (durationSec ? durationSec : ALARM_DEFAULT_SEC) * 1000UL;

  restart_scroll();
  return true;
}

void display_dismiss_alarm() {
  if (gState != STATE_ALARM) return;
  gState = STATE_RUNNING;
  gFmt   = gSavedFmt;
  strncpy(gMsgBuf, gSavedMsg, MAX_MSG_LEN); gMsgBuf[MAX_MSG_LEN] = '\0';
  gShowingPlaceholder = gSavedWasPlaceholder;
  // A message queued while the alarm was active must surface now, even if
  // the mode active before the alarm was not MODE_MESSAGE (mirrors the
  // boot-sequence promotion in display_tick()).
  if (gQueueFull && gMsgBuf[0] == '\0') {
    gMode = MODE_MESSAGE;
  } else {
    gMode = gSavedMode;
  }
  display_set_mode(gMode);
}

bool display_is_alarm_active() { return gState == STATE_ALARM; }

// ─────────────────────────────────────────────────────────────────────────────
// Tick — the whole state machine
// ─────────────────────────────────────────────────────────────────────────────
void display_tick() {

  if (gState == STATE_PROVISION) {
    if (P.displayAnimate()) {
      // Loop the persistent copy of whatever display_show_provisioning() was
      // last called with (see gProvisionBuf above) — MD_Parola has no way to
      // read the current text back out, so display.cpp must keep its own copy.
      P.displayText(gProvisionBuf, PA_LEFT, gSpeed, 0,
                    PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    }
    return;
  }

  if (gState == STATE_BOOT) {
    if (P.displayAnimate()) {
      gBootIdx++;
      if (gBootIdx < BOOT_MSG_COUNT) {
        P.displayText(BOOT_MSGS[gBootIdx], PA_LEFT, gSpeed, 0,
                      PA_SCROLL_LEFT, PA_SCROLL_LEFT);
      } else {
        if (gDeferClear) {
          gDeferClear = false;
          gMsgBuf[0]   = '\0';
          gQueueBuf[0] = '\0';
          gQueueFull   = false;
        }
        gState = STATE_RUNNING;
        if (gQueueFull && gMsgBuf[0] == '\0') {
          promote_queue();
          gMode = MODE_MESSAGE;
          restart_scroll();
        } else if (gMode == MODE_CLOCK) {
          clock_refresh();
        } else if (gMode == MODE_MESSAGE) {
          display_set_mode(MODE_MESSAGE);   // shows the placeholder if nothing is set
        }
      }
    }
    return;
  }

  if (gState == STATE_ALARM) {
    if (millis() >= gAlarmUntil) { display_dismiss_alarm(); return; }
    if (gComposite) composite_tick();
    else if (P.displayAnimate()) restart_scroll();
    return;
  }

  // ── STATE_RUNNING ────────────────────────────────────────────────────────
  switch (gMode) {
    case MODE_MESSAGE:
      if (gMsgBuf[0] == '\0') break;     // idle; frame is already blank
      if (gComposite) composite_tick();
      else if (P.displayAnimate()) {
        gPass++;
        if (gPass >= SCROLL_PASSES) on_pass_complete();
        else                        apply_anim();
      }
      break;

    case MODE_CLOCK:
      clock_refresh();
      P.displayAnimate();
      break;

    case MODE_CLOCK_DATE:
      if (gFmt == FMT_NAMED) {
        if (gComposite) composite_tick();
        else if (P.displayAnimate()) {
          gPass++;
          if (gPass >= SCROLL_PASSES) on_pass_complete();
          else                        apply_anim();
        }
      } else {
        clock_date_static_tick();
      }
      break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static const char* active_scroll_text() {
  if (gState == STATE_ALARM)                              return gAlarmBuf;
  if (gMode == MODE_CLOCK_DATE && gFmt == FMT_NAMED)      return gClockBuf;
  return gMsgBuf;
}

static bool scrolling_active() {
  if (gState == STATE_ALARM)                              return true;
  if (gMode == MODE_MESSAGE && gMsgBuf[0] != '\0')        return true;
  if (gMode == MODE_CLOCK_DATE && gFmt == FMT_NAMED)      return true;
  return false;
}

void restart_scroll() {
  gPass = 0;
  const char* txt = active_scroll_text();
  if (!txt || !txt[0]) return;

  gComposite = (gAnim >= PAROLA_ANIM_COUNT);
  if (gComposite) {
    build_shadow_buf(txt);
    compScrollX   = -(int32_t)DISPLAY_COLS;
    compScrollTS  = millis();
    compFxTS      = millis();
    cursorVisible = false;
    scanRow       = 0;
  } else {
    apply_anim();
  }
}

static void apply_anim() {
  const char* txt = active_scroll_text();
  const AnimPreset& a = ANIM_PRESETS[gAnim];
  P.displayText((char*)txt, a.align, gSpeed, a.pause,
                a.effectIn, a.effectOut);
}

void promote_queue() {
  strncpy(gMsgBuf, gQueueBuf, MAX_MSG_LEN);
  gMsgBuf[MAX_MSG_LEN] = '\0';
  gQueueBuf[0] = '\0';
  gQueueFull   = false;
  gShowingPlaceholder = false;
}

void on_pass_complete() {
  if (gState == STATE_ALARM) {
    // Alarms loop until dismissed or their timer expires.
  } else if (gMode == MODE_MESSAGE) {
    if (gPassCb && !gShowingPlaceholder) gPassCb();  // protocol layer broadcasts D:
    if (gQueueFull) promote_queue();
  } else if (gMode == MODE_CLOCK_DATE) {
    strncpy(gClockBuf, clock_named_datetime().c_str(), MAX_MSG_LEN);
    gClockBuf[MAX_MSG_LEN] = '\0';
  }
  restart_scroll();
}

// ─────────────────────────────────────────────────────────────────────────────
// Clock / date
// ─────────────────────────────────────────────────────────────────────────────
static void clock_refresh() {
  static unsigned long last = 0;
  if (millis() - last < RTC_REFRESH_MS) return;
  last = millis();

  String t = clock_time_str();
  if (t == gLastClockStr) return;      // fix A5: no needless repaint
  t.toCharArray(gLastClockStr, sizeof(gLastClockStr));

  P.displayClear();
  P.displayText(gLastClockStr, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  P.displayAnimate();
}

static void clock_date_repaint() {
  char buf[12];
  if (gClockShowTime) {
    clock_time_str().toCharArray(buf, sizeof(buf));
  } else {
    clock_numeric_date(gFmt, buf, sizeof(buf));
  }
  // displayClear() before switching to a static PA_PRINT frame is the
  // library-documented way to interrupt a running effect; without it the
  // transition from a scroll (named) format leaves the panel dark.
  P.displayClear();
  P.displayText(buf, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  P.displayAnimate();
}

static void clock_date_static_tick() {
  unsigned long now = millis();
  if (now - gClockAltTS >= CLOCK_DATE_SWITCH_MS) {
    gClockAltTS    = now;
    gClockShowTime = !gClockShowTime;
    clock_date_repaint();
  } else {
    P.displayAnimate();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Composite effects
// ─────────────────────────────────────────────────────────────────────────────
static void build_shadow_buf(const char* msg) {
  MD_MAX72XX* mx = P.getGraphicObject();
  uint8_t charData[16];
  shadowWidth = 0;

  for (uint16_t i = 0; msg[i] != '\0'; i++) {
    uint8_t cols = mx->getChar((uint8_t)msg[i], sizeof(charData), charData);
    for (uint8_t c = 0; c < cols && shadowWidth < SHADOW_BUF_MAX - 1; c++) {
      shadowBuf[shadowWidth++] = charData[c];
    }
    if (msg[i + 1] != '\0' && shadowWidth < SHADOW_BUF_MAX - SCROLL_SPACING) {
      for (uint8_t s = 0; s < SCROLL_SPACING; s++)
        shadowBuf[shadowWidth++] = 0x00;   // fix A7: honour scroll spacing
    }
  }
}

static void composite_tick() {
  unsigned long now = millis();

  if (now - compScrollTS >= (unsigned long)gSpeed) {
    compScrollTS = now;
    compScrollX++;

    if (compScrollX >= (int32_t)shadowWidth) {
      gPass++;
      if (gPass >= SCROLL_PASSES) {
        on_pass_complete();
        return;
      }
      compScrollX   = -(int32_t)DISPLAY_COLS;
      compScrollTS  = now;        // fix B2: reset both timers
      compFxTS      = now;
      cursorVisible = false;
      scanRow       = 0;
    }
  }

  uint8_t dispBuf[DISPLAY_COLS] = { 0 };
  for (uint8_t d = 0; d < DISPLAY_COLS; d++) {
    int32_t src = compScrollX + (int32_t)d;
    if (src >= 0 && src < (int32_t)shadowWidth) dispBuf[d] = shadowBuf[src];
  }

  if (gAnim == ANIM_COMPOSITE_CURSOR) {
    if (now - compFxTS >= CURSOR_BLINK_MS) {
      compFxTS      = now;
      cursorVisible = !cursorVisible;
    }
    if (cursorVisible) {
      int16_t leadCol = -1;
      for (int16_t d = DISPLAY_COLS - 1; d >= 0; d--) {
        if (dispBuf[d] != 0x00) { leadCol = d; break; }
      }
      if (leadCol >= 0) dispBuf[leadCol] = 0xFF;
    }
  } else {   // ANIM_COMPOSITE_SCANH
    if (now - compFxTS >= SCAN_STEP_MS) {
      compFxTS = now;
      scanRow  = (scanRow + 1) & 0x07;
    }
    uint8_t mask = (uint8_t)((1 << (scanRow + 1)) - 1);
    for (uint8_t d = 0; d < DISPLAY_COLS; d++) dispBuf[d] &= mask;
  }

  MD_MAX72XX* mx = P.getGraphicObject();
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  for (uint8_t d = 0; d < DISPLAY_COLS; d++) {
    mx->setColumn(DISPLAY_COLS - 1 - d, dispBuf[d]);
  }
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}