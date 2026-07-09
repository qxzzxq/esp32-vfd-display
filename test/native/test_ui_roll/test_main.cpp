// Roll-animation tests: the 5x7 font, the split-flap frame compositor, the
// UiOutput glyph channel, and the FSM roll trigger (opted-in content changes
// — TIME ticks — roll their changed cells in lockstep with slot sharing;
// input snaps a roll; overlays suppress it). Page changes crossfade instead of
// rolling — that envelope is pinned in test_ui_fade. Timelines advance in the
// shell's 30 ms animation frames via FsmDriver::idle_us/settle.
#include <string.h>
#include <unity.h>

#include "../ui_test_helpers.h"
#include "ui/font5x7.h"
#include "ui/glyphs.h"
#include "ui/roll.h"

void setUp(void) {}
void tearDown(void) {}

static const char* TIME_LINE = "    14:25:36    ";
static const char* DATE_LINE = " SAT 2026-07-05 ";
static const char* INDOOR_LINE = "IN  23.4C   47% ";

static void assert_line(const UiOutput& out, const char* expect) {
    TEST_ASSERT_EQUAL_STRING(expect, out.line);
}

// ---- font ----

static void test_font_all_printable_bit7_clear(void) {
    for (int c = 0x20; c <= 0x7E; c++) {
        const uint8_t* g = ui_font_glyph((char)c);
        for (int i = 0; i < 5; i++) TEST_ASSERT_EQUAL_UINT8(0, g[i] & 0x80);
    }
}

static void test_font_out_of_range_is_space(void) {
    const uint8_t* space = ui_font_glyph(' ');
    TEST_ASSERT_EQUAL_UINT8_ARRAY(space, ui_font_glyph('\x01'), 5);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(space, ui_font_glyph('\x7F'), 5);
    for (int i = 0; i < 5; i++) TEST_ASSERT_EQUAL_UINT8(0, space[i]);
}

static void test_font_spot_checks(void) {
    static const uint8_t ZERO[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
    static const uint8_t AY[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
    static const uint8_t COLON[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ZERO, ui_font_glyph('0'), 5);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(AY, ui_font_glyph('A'), 5);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(COLON, ui_font_glyph(':'), 5);
}

// ---- compositor ----

static void test_composite_endpoints_are_pure_glyphs(void) {
    const char pairs[][2] = {{'0', '1'}, {'A', 'Z'}, {' ', 'P'}};
    for (auto& p : pairs) {
        uint8_t cols[5];
        ui_roll_composite(p[0], p[1], 0, cols);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(ui_font_glyph(p[0]), cols, 5);
        ui_roll_composite(p[0], p[1], UI_ROLL_STEPS, cols);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(ui_font_glyph(p[1]), cols, 5);
    }
}

static void test_composite_mid_frames(void) {
    // Hand-derived from the '0' and '1' columns: at k the old glyph has
    // shifted up k rows; the new glyph's top row sits k-1 rows into view
    // (one blank gap row between them).
    static const uint8_t K3[5] = {0x07, 0x4A, 0x69, 0x08, 0x07};
    static const uint8_t K4[5] = {0x03, 0x25, 0x74, 0x04, 0x03};
    uint8_t cols[5];
    ui_roll_composite('0', '1', 3, cols);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(K3, cols, 5);
    ui_roll_composite('0', '1', 4, cols);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(K4, cols, 5);
}

// ---- glyph channel ----

static void test_idle_tick_carries_default_glyphs(void) {
    FsmDriver d;
    d.idle();
    TEST_ASSERT_FALSE(d.out.animating);
    for (int i = 0; i < UI_GLYPH_COUNT; i++)
        TEST_ASSERT_EQUAL_UINT8_ARRAY(UI_GLYPHS[i].cols,
                                      d.out.glyphs[UI_GLYPHS[i].slot], 5);
    static const uint8_t ZEROS[5] = {0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ZEROS, d.out.glyphs[7], 5);  // only slot 7 unused now
}

// ---- TIME lockstep roll ----

static void test_time_second_roll_timeline(void) {
    FsmDriver d;
    assert_line(d.idle(), TIME_LINE);  // boot frame snaps
    d.snap.tm_now.tm_sec = 37;
    // Trigger frame: roll starts at k=0, still showing the old content.
    assert_line(d.idle_us(30000), TIME_LINE);
    TEST_ASSERT_TRUE(d.out.animating);
    // Interior frames: the seconds cell shows CGRAM slot 1 with the
    // '6'->'7' composite for the current step.
    uint8_t expect[5];
    for (int k = 1; k < UI_ROLL_STEPS; k++) {
        assert_line(d.idle_us(30000), "    14:25:3\x01    ");
        ui_roll_composite(k <= 6 ? '6' : ' ', k >= 2 ? '7' : ' ', k, expect);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, d.out.glyphs[1], 5);
    }
    // Final frame: pure ASCII again, defaults restored, animation over.
    assert_line(d.idle_us(30000), "    14:25:37    ");
    TEST_ASSERT_FALSE(d.out.animating);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(UI_GLYPHS[0].cols, d.out.glyphs[1], 5);
}

static void test_minute_rollover_rolls_cells_in_lockstep(void) {
    FsmDriver d;
    d.snap.tm_now.tm_sec = 59;
    d.idle();  // boot frame: 14:25:59
    d.snap.tm_now.tm_min = 26;
    d.snap.tm_now.tm_sec = 0;
    d.idle_us(30000);  // trigger frame (old content)
    // Three changed cells (min units, sec tens, sec units) roll together.
    // At k = 1 the incoming glyphs are not yet visible, so the two cells
    // exiting a '5' share one slot; from k = 2 the keys are full pairs.
    assert_line(d.idle_us(30000), "    14:2\x01:\x01\x02    ");
    assert_line(d.idle_us(30000), "    14:2\x01:\x02\x03    ");
    d.settle();
    assert_line(d.out, "    14:26:00    ");
}

static void test_over_budget_lockstep_flips_excess_at_midpoint(void) {
    // A 24H format flip is a content roll changing 10 cells with 10 distinct
    // from->to pairs. At k = 1 only the exiting chars are visible (7 distinct
    // -> everything fits, ':' cells share); from k = 2 the keys are full
    // pairs, so the three rightmost changed cells hold their old chars and
    // flip at the midpoint (k = 4).
    FsmDriver d;
    assert_line(d.idle(), TIME_LINE);
    d.snap.use24h = false;
    d.idle_us(30000);  // trigger
    assert_line(d.idle_us(30000), "    \x01\x02\x03\x04\x05\x03\x06\x07    ");
    assert_line(d.idle_us(30000), "   \x01\x02\x03\x04\x05\x06\x07" "36    ");
    d.idle_us(30000, 2);  // k = 4
    assert_line(d.out, "   \x01\x02\x03\x04\x05\x06\x07 PM   ");
    d.settle();
    assert_line(d.out, "   2:25:36 PM   ");
}

static void test_identical_pairs_share_one_slot(void) {
    FsmDriver d;
    d.snap.tm_now.tm_hour = 11;
    d.snap.tm_now.tm_min = 11;
    d.snap.tm_now.tm_sec = 11;
    assert_line(d.idle(), "    11:11:11    ");
    d.snap.tm_now.tm_hour = 22;
    d.snap.tm_now.tm_min = 22;
    d.snap.tm_now.tm_sec = 22;
    d.idle_us(30000);  // trigger
    // All six changing digits are the same '1'->'2' pair -> one shared slot.
    assert_line(d.idle_us(30000), "    \x01\x01:\x01\x01:\x01\x01    ");
    uint8_t expect[5];
    ui_roll_composite('1', ' ', 1, expect);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, d.out.glyphs[1], 5);
    // Slot 2 was never needed -> still carries its default bar glyph.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(UI_GLYPHS[1].cols, d.out.glyphs[2], 5);
    d.settle();
    assert_line(d.out, "    22:22:22    ");
}

// ---- interaction with input and overlays ----

static void test_input_mid_roll_snaps_then_processes(void) {
    FsmDriver d;
    d.idle();
    d.snap.tm_now.tm_sec = 37;
    d.idle_us(30000, 3);  // TIME roll mid-flight
    TEST_ASSERT_TRUE(d.out.animating);
    // The step snaps the roll and processes the page change; once the ensuing
    // crossfade settles the display is on the next page.
    d.feed(UiInput::StepCW);
    d.settle();
    assert_line(d.out, DATE_LINE);
    TEST_ASSERT_FALSE(d.out.animating);
}

static void test_btndown_mid_roll_cancels_before_hold_bar(void) {
    FsmDriver d;
    d.idle();
    d.snap.tm_now.tm_sec = 37;
    d.idle_us(30000, 3);  // TIME roll mid-flight
    assert_line(d.feed(UiInput::BtnDown), "    14:25:37    ");  // snapped
    TEST_ASSERT_FALSE(d.out.animating);
    d.idle(5);  // held 500 ms: the hold bar begins crossfading in
    TEST_ASSERT_TRUE(d.out.animating);
}

static void test_portal_banner_suppresses_roll(void) {
    FsmDriver d;
    d.snap.net = UiNetState::Portal;
    d.idle();
    d.feed(UiInput::StepCW);  // page steps underneath the banner (no roll)
    TEST_ASSERT_FALSE(d.out.animating);
    d.snap.net = UiNetState::Connected;
    // Leaving the portal crossfades to the stepped-to page (not a roll).
    d.idle();
    assert_line(d.settle(), DATE_LINE);
    TEST_ASSERT_FALSE(d.out.animating);
}

static void test_menu_carries_arrow_glyph(void) {
    FsmDriver d;
    d.long_press();
    d.settle();  // crossfade into the menu (arrow glyph rides the default channel)
    TEST_ASSERT_EQUAL_UINT8_ARRAY(UI_GLYPHS[UI_GLYPH_ARROW - 1].cols,
                                  d.out.glyphs[UI_GLYPH_ARROW], 5);
    d.step(UiInput::StepCCW);  // EXIT
    d.click();                 // exit crossfades out of the menu (no roll)
    assert_line(d.settle(), TIME_LINE);
    TEST_ASSERT_FALSE(d.out.animating);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_font_all_printable_bit7_clear);
    RUN_TEST(test_font_out_of_range_is_space);
    RUN_TEST(test_font_spot_checks);
    RUN_TEST(test_composite_endpoints_are_pure_glyphs);
    RUN_TEST(test_composite_mid_frames);
    RUN_TEST(test_idle_tick_carries_default_glyphs);
    RUN_TEST(test_time_second_roll_timeline);
    RUN_TEST(test_minute_rollover_rolls_cells_in_lockstep);
    RUN_TEST(test_over_budget_lockstep_flips_excess_at_midpoint);
    RUN_TEST(test_identical_pairs_share_one_slot);
    RUN_TEST(test_input_mid_roll_snaps_then_processes);
    RUN_TEST(test_btndown_mid_roll_cancels_before_hold_bar);
    RUN_TEST(test_portal_banner_suppresses_roll);
    RUN_TEST(test_menu_carries_arrow_glyph);
    return UNITY_END();
}
