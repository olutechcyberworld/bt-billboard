#include "protocol.h"
#include "config.h"
#include "display.h"
#include "clock.h"
#include "buzzer.h"
#include "scheduler.h"
#include "transport.h"

// Split a string on a single character into at most `maxParts` fields.
// The final field always receives the rest of the string (so messages may
// contain the delimiter).
static uint8_t split(const String& s, char sep, String* out, uint8_t maxParts) {
  uint8_t n = 0;
  int     start = 0;
  for (int i = 0; i < (int)s.length() && n < maxParts - 1; i++) {
    if (s.charAt(i) == sep) {
      out[n++] = s.substring(start, i);
      start = i + 1;
    }
  }
  out[n++] = s.substring(start);
  return n;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass-complete callback (D:) — broadcast because it is not tied to a specific
// client, unlike acks which go back to the sender.
static void on_display_pass() { transport_broadcast("D:\n"); }

// ─────────────────────────────────────────────────────────────────────────────
void protocol_dispatch(uint8_t clientNum, const String& frame) {
  if (frame.length() < 1) return;

  const char   ftype   = frame.charAt(0);
  const String payload = (frame.length() > 2 && frame.charAt(1) == ':')
                         ? frame.substring(2) : String("");
  static bool  cbInstalled = false;
  if (!cbInstalled) { display_set_pass_callback(on_display_pass); cbInstalled = true; }

  Serial.printf("[RX] %s\n", frame.c_str());

  switch (ftype) {

    // ── Message ─────────────────────────────────────────────────────────────
    case 'T': {
      if (payload.length() == 0) return;
      bool accepted = false;
      display_set_message(payload.c_str(), &accepted);
      transport_reply(clientNum, accepted ? "A:0\n" : "A:1\n");
      break;
    }

    // ── Clear ───────────────────────────────────────────────────────────────
    case 'C': {
      // Dismiss any live alarm before honouring the clear.
      if (display_is_alarm_active()) display_dismiss_alarm();
      buzzer_stop();
      display_clear();
      transport_reply(clientNum, "X:\n");
      break;
    }

    // ── Time sync ───────────────────────────────────────────────────────────
    case 'S': {
      uint16_t y; uint8_t mo, d, h, mi, s;
      // Fix C7: strict parsing. Reject anything malformed outright rather
      // than handing junk to RTClib.
      if (sscanf(payload.c_str(), "%hu-%hhu-%hhu %hhu:%hhu:%hhu",
                 &y, &mo, &d, &h, &mi, &s) != 6 ||
          y < 2024 || mo < 1 || mo > 12 || d < 1 || d > 31 ||
          h > 23 || mi > 59 || s > 59) {
        Serial.println(F("[TIME] Malformed S: payload rejected"));
        break;
      }
      transport_reply(clientNum, clock_set(y, mo, d, h, mi, s) ? "T:OK\n" : "T:ERR\n");
      break;
    }

    // ── Speed ───────────────────────────────────────────────────────────────
    case 'P': {
      long pct = payload.toInt();
      if (pct < 0) pct = 0; if (pct > 100) pct = 100;
      display_set_speed((uint8_t)pct);
      Serial.printf("[SPEED] %ld%%\n", pct);
      break;
    }

    // ── Brightness ──────────────────────────────────────────────────────────
    case 'B': {
      long pct = payload.toInt();
      if (pct < 0) pct = 0; if (pct > 100) pct = 100;
      display_set_brightness((uint8_t)pct);
      Serial.printf("[BRIGHT] %ld%%\n", pct);
      break;
    }

    // ── Animation ───────────────────────────────────────────────────────────
    case 'N': {
      long idx = payload.toInt();
      if (idx < 0 || idx >= ANIM_COUNT) { Serial.println(F("[ANIM] Out of range")); break; }
      display_set_anim((uint8_t)idx);
      Serial.printf("[ANIM] %ld\n", idx);
      break;
    }

    // ── Mode ────────────────────────────────────────────────────────────────
    case 'M': {
      long m = payload.toInt();
      if (m < 0 || m > 2) { Serial.println(F("[MODE] Out of range")); break; }
      display_set_mode((DisplayMode)m);
      transport_reply(clientNum, "M:OK\n");
      break;
    }

    // ── Date format ─────────────────────────────────────────────────────────
    case 'F': {
      long f = payload.toInt();
      if (f < 0 || f > 3) { Serial.println(F("[FMT] Out of range")); break; }
      display_set_date_format((DateFormat)f);
      transport_reply(clientNum, "F:OK\n");
      break;
    }

    // ── Font selection ──────────────────────────────────────────────────────
    case 'K': {
      long id = payload.toInt();
      if (!display_set_font((uint8_t)id)) {
        transport_reply(clientNum, "K:ERR\n");
        break;
      }
      transport_reply(clientNum, "K:OK\n");
      Serial.printf("[FONT] -> %ld\n", id);
      break;
    }

    // ── Schedule write ──────────────────────────────────────────────────────
    case 'W': {
      if (payload == "!") { scheduler_clear(); transport_reply(clientNum, "W:OK\n"); break; }

      String parts[8];
      uint8_t n = split(payload, ',', parts, 8);
      if (n < 2) { transport_reply(clientNum, "W:ERR\n"); break; }

      long idx = parts[0].toInt();
      if (idx < 0 || idx >= MAX_SCHEDULES) { transport_reply(clientNum, "W:ERR\n"); break; }

      if (parts[1] == "-") {
        scheduler_delete((uint8_t)idx);
        transport_reply(clientNum, "W:OK\n");
        break;
      }

      if (n < 8) { transport_reply(clientNum, "W:ERR\n"); break; }

      ScheduleEntry e = {};
      e.enabled     = (uint8_t)(parts[1].toInt() ? 1 : 0);
      long hhmm     = parts[2].toInt();
      e.hour        = (uint8_t)(hhmm / 100);
      e.minute      = (uint8_t)(hhmm % 100);
      if (e.hour > 23 || e.minute > 59) { transport_reply(clientNum, "W:ERR\n"); break; }

      e.daysMask = 0;
      const String& days = parts[3];
      for (uint8_t i = 0; i < 7 && i < days.length(); i++)
        if (days.charAt(i) == '1') e.daysMask |= (1 << i);

      long act  = parts[4].toInt();
      long buzz = parts[5].toInt();
      long dur  = parts[6].toInt();
      if (act < 0 || act > 2 || buzz < 0 || buzz > 4) {
        transport_reply(clientNum, "W:ERR\n"); break;
      }
      e.action      = (uint8_t)act;
      e.buzzPattern = (uint8_t)buzz;
      e.durationSec = (uint8_t)(dur < 0 ? 0 : (dur > 255 ? 255 : dur));

      strncpy(e.message, parts[7].c_str(), SCHED_MSG_LEN - 1);
      e.message[SCHED_MSG_LEN - 1] = '\0';

      scheduler_set((uint8_t)idx, e);
      transport_reply(clientNum, "W:OK\n");
      break;
    }

    // ── Schedule list ───────────────────────────────────────────────────────
    case 'L': {
      char line[160];
      for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
        const ScheduleEntry* e = scheduler_get(i);
        if (!e) continue;
        char days[8];
        for (uint8_t d = 0; d < 7; d++) days[d] = (e->daysMask & (1 << d)) ? '1' : '0';
        days[7] = '\0';
        snprintf(line, sizeof(line), "L:%u,%u,%02u%02u,%s,%u,%u,%u,%s\n",
                 i, e->enabled, e->hour, e->minute, days,
                 e->action, e->buzzPattern, e->durationSec, e->message);
        transport_reply(clientNum, line);
      }
      transport_reply(clientNum, "L:END\n");
      break;
    }

    // ── Device info ─────────────────────────────────────────────────────────
    case 'I': {
      char line[96];
      snprintf(line, sizeof(line), "I:%s,%s,%s\n",
               transport_mac(), transport_hostname(), FW_VERSION);
      transport_reply(clientNum, line);
      break;
    }

    default:
      Serial.printf("[FRAME] Unknown '%c'\n", ftype);
      break;
  }
}