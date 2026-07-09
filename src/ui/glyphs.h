#ifndef ui_glyphs_h
#define ui_glyphs_h

// CGRAM glyph contract: recreates the 8-bit VFD's (8-MD-06INKM) CGROM symbols
// that the 16-SD-13GINK lacks — the 0x11..0x15 progress fills and the 0x0B
// solid right arrow. DCRAM character codes 0x00..0x07 select CGRAM slots
// directly, so the code embedded in a line IS the slot number. 0x00 is
// unusable (UiOutput::line is a NUL-terminated C string); usable codes are
// 0x01..0x07.
//
// Bar cells encode their fill in the code itself: code n = n lit columns
// (n = 1..4). A fully lit cell is the CGROM solid block 0x7F — no slot spent.
//
// The shell uploads UI_GLYPHS after VFDDisplay::init() (controller reset
// clears CGRAM). Column format: 5 bytes left->right, bit0 = top pixel,
// bit6 = bottom row (16-SD-13GINK datasheet, section 2.2).

#include <stdint.h>

constexpr char UI_GLYPH_BAR_FULL = '\x7F';  // CGROM solid block
constexpr char UI_GLYPH_ARROW = '\x05';     // menu cursor

struct UiGlyphDef {
    uint8_t slot;     // CGRAM address == DCRAM character code
    uint8_t cols[5];  // bit7 must stay clear (only 7 pixel rows exist)
};

constexpr UiGlyphDef UI_GLYPHS[] = {
    {1, {0x7F, 0x00, 0x00, 0x00, 0x00}},  // bar, 1 column lit
    {2, {0x7F, 0x7F, 0x00, 0x00, 0x00}},  // bar, 2 columns
    {3, {0x7F, 0x7F, 0x7F, 0x00, 0x00}},  // bar, 3 columns
    {4, {0x7F, 0x7F, 0x7F, 0x7F, 0x00}},  // bar, 4 columns
    {5, {0x7F, 0x3E, 0x1C, 0x08, 0x00}},  // solid right arrow
};

constexpr int UI_GLYPH_COUNT = sizeof(UI_GLYPHS) / sizeof(UI_GLYPHS[0]);

// Compile-time sanity: slot must equal index + 1 (bar code == lit-column
// count, which render_hold_bar relies on; also forces unique codes 1..7),
// and no column byte may set bit7.
constexpr bool ui_glyphs_valid() {
    if (UI_GLYPH_COUNT > 7) return false;
    for (int i = 0; i < UI_GLYPH_COUNT; i++) {
        if (UI_GLYPHS[i].slot != i + 1) return false;
        for (int c = 0; c < 5; c++)
            if (UI_GLYPHS[i].cols[c] & 0x80) return false;
    }
    return true;
}
static_assert(ui_glyphs_valid(), "CGRAM glyph table inconsistent");
static_assert(UI_GLYPHS[UI_GLYPH_ARROW - 1].slot == UI_GLYPH_ARROW,
              "arrow code must select the arrow slot");

#endif
