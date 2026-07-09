#ifndef ui_roll_h
#define ui_roll_h

// Vertical roll (split-flap) frame compositor. A cell's animation is a 7-row
// window sliding over a virtual strip of [from glyph][1 blank gap row]
// [to glyph]; step k in [0, UI_ROLL_STEPS] selects the window position.
// k = 0 renders exactly the from glyph, k = UI_ROLL_STEPS exactly the to
// glyph; the from glyph exits through the top while the to glyph enters from
// below.

#include <stdint.h>

void ui_roll_composite(char from, char to, int k, uint8_t cols[5]);

#endif
