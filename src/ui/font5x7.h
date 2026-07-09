#ifndef ui_font5x7_h
#define ui_font5x7_h

// 5x7 font used to composite roll-animation frames (the CGROM font is not
// readable back from the controller). Same layout as CGRAM patterns: 5 bytes
// per glyph, one column left->right, bit0 = top pixel .. bit6 = bottom, bit7
// always clear. Shapes follow the classic HD44780-style 5x7 set, which this
// tube's CGROM visually matches for the characters the UI uses — mismatched
// glyphs would "pop" at the end of a roll and get patched here by eye.

#include <stdint.h>

// Returns the 5 columns for a printable ASCII char (0x20..0x7E); anything
// else renders as space.
const uint8_t* ui_font_glyph(char c);

#endif
