#include "fonts.h"

// ─────────────────────────────────────────────────────────────────────────────
// Font registry.
//
// Slot 0 is intentionally `nullptr`: it tells display_set_font() to leave the
// MD_MAX72XX built-in 5x7 font untouched. That is the safest default because
// the library's own font data is not exposed for direct referencing.
//
// To add a new font:
//   1. Paste its PROGMEM byte array into a header, e.g. `font_compact.h`.
//      MD_MAX72XX font format:
//          byte 0            = pixel height (must be <= 8 for a single row)
//          then, per glyph from ASCII 0x20 upward:
//              N column bytes (bit 0 = top row) followed by a single 0x00
//              terminator. 'N' is the glyph's rendered width; the terminator
//              is not part of the glyph.
//   2. #include "font_compact.h" below.
//   3. Append { "Compact", font_compact } to FONT_REGISTRY.
//
// Plenty of ready-made MD_MAX72XX-compatible fonts exist (search
// "MD_MAX72XX font" on GitHub); they drop in unchanged.
// ─────────────────────────────────────────────────────────────────────────────

const FontEntry FONT_REGISTRY[] = {
  { "Default", nullptr },
  // { "Compact", font_compact },   // <- uncomment once a font blob is provided
};

const uint8_t FONT_REGISTRY_COUNT =
    (uint8_t)(sizeof(FONT_REGISTRY) / sizeof(FONT_REGISTRY[0]));