#include "roll.h"

#include "core.h"
#include "font5x7.h"

void ui_roll_composite(char from, char to, int k, bool upward, uint8_t cols[5]) {
    const uint8_t* f = ui_font_glyph(from);
    const uint8_t* t = ui_font_glyph(to);
    for (int i = 0; i < 5; i++) {
        // bit0 is the top row, so ">>" moves pixels up. Shifting the incoming
        // glyph by (STEPS - k) leaves the gap row blank between the two.
        unsigned v = upward ? ((unsigned)f[i] >> k) | ((unsigned)t[i] << (UI_ROLL_STEPS - k))
                            : ((unsigned)f[i] << k) | ((unsigned)t[i] >> (UI_ROLL_STEPS - k));
        cols[i] = (uint8_t)(v & 0x7F);
    }
}
