#include "fonts.h"
#include "font_classic5x7.h"
#include "font_bold5x7.h"

// ─────────────────────────────────────────────────────────────────────────────
// Font registry.
//
// Slot 0 is intentionally `nullptr`: it tells display_set_font() to leave the
// MD_MAX72XX built-in 5x7 font untouched. That is the safest default because
// the library's own font data is not exposed for direct referencing.
//
// To add a new font:
//   1. Paste its PROGMEM byte array into a header, e.g. `font_compact.h`.
//      MD_MAX72XX "version 0" font format (no 'F' header token, the format
//      used throughout this project): a CONTIGUOUS sequence of
//          [sizeByte][sizeByte data bytes]
//      one such block per ASCII code 0..255, back to back, with NO separator
//      or terminator byte of any kind between glyphs. The library's own
//      getFontCharOffset() walks the table using only each glyph's own size
//      byte to know how many bytes to skip to reach the next one:
//          offset += pgm_read_byte(_fontData + offset);  // add size
//          offset++;                                     // skip size byte
//      An unused code point is simply a lone size-0 byte (`0,`) -- that IS
//      the complete entry for that code point, not a terminator following
//      some other glyph. Inserting an extra byte after any glyph's data
//      (as an earlier version of this comment incorrectly described) shifts
//      every subsequent character's offset by one and corrupts the whole
//      font from that point on. See font_classic5x7.h for a verified,
//      working example of this exact layout.
//      Column byte convention: bit 0 of each column byte is the top row.
//   2. #include "font_compact.h" below.
//   3. Append { "Compact", font_compact } to FONT_REGISTRY.
//
// Plenty of ready-made MD_MAX72XX-compatible fonts exist (search
// "MD_MAX72XX font" on GitHub); they drop in unchanged as long as they use
// this same version-0 layout. Check any font you add against a real MAX7219
// module before shipping -- getFontCharOffset()'s cumulative walk means a
// single malformed glyph anywhere in the table corrupts every character
// after it, not just that one glyph.
// ─────────────────────────────────────────────────────────────────────────────

const FontEntry FONT_REGISTRY[] = {
  { "Default",    nullptr },
  { "Classic5x7", font_classic5x7 },
  { "Bold5x7",    font_bold5x7 },
};

const uint8_t FONT_REGISTRY_COUNT =
    (uint8_t)(sizeof(FONT_REGISTRY) / sizeof(FONT_REGISTRY[0]));