#ifndef ui_roll_h
#define ui_roll_h

// Vertical roll (split-flap) frame compositor. A cell's animation is a 7-row
// window sliding over a virtual strip of [from glyph][steps-7 blank gap rows]
// [to glyph]; step k in [0, steps] selects the window position. k = 0 renders
// exactly the from glyph, k = steps exactly the to glyph. Upward: from exits
// through the top, to enters from below (downward mirrors both). With
// steps = 14 (7-row gap) the two glyphs are never visible together: the from
// glyph is gone after k = 6 and the to glyph absent before k = 8, which is
// what lets page rolls share CGRAM slots per character.

#include <stdint.h>

void ui_roll_composite(char from, char to, int k, bool upward, int steps,
                       uint8_t cols[5]);

#endif
