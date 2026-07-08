// Roll-animation tests: the 5x7 font, the split-flap frame compositor, the
// UiOutput glyph channel, and the FSM roll triggers (TIME lockstep roll,
// page-change wave, snap-on-input, overlay suppression). Timelines advance
// in the shell's 40 ms animation frames via FsmDriver::idle_us/settle.
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
static const char* PRESSURE_LINE = "PRES 1013.2 hPa ";
static const char* CUSTOM_LINE = "       HI       ";

static void assert_line(const UiOutput& out, const char* expect) {
    TEST_ASSERT_EQUAL_STRING(expect, out.line);
}

// Step to the next page and let the wave finish.
static const UiOutput& step_settled(FsmDriver& d, UiInput in) {
    d.feed(in);
    return d.settle();
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
        for (int up = 0; up <= 1; up++) {
            uint8_t cols[5];
            ui_roll_composite(p[0], p[1], 0, up, cols);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(ui_font_glyph(p[0]), cols, 5);
            ui_roll_composite(p[0], p[1], UI_ROLL_STEPS, up, cols);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(ui_font_glyph(p[1]), cols, 5);
        }
    }
}

static void test_composite_upward_mid_frames(void) {
    // Hand-derived from the '0' and '1' columns: at k the old glyph has
    // shifted up k rows; the new glyph's top row sits k-1 rows into view
    // (one blank gap row between them).
    static const uint8_t K3[5] = {0x07, 0x4A, 0x69, 0x08, 0x07};
    static const uint8_t K4[5] = {0x03, 0x25, 0x74, 0x04, 0x03};
    uint8_t cols[5];
    ui_roll_composite('0', '1', 3, true, cols);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(K3, cols, 5);
    ui_roll_composite('0', '1', 4, true, cols);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(K4, cols, 5);
}

static void test_composite_downward_mid_frame(void) {
    static const uint8_t K2[5] = {0x78, 0x45, 0x25, 0x15, 0x78};
    uint8_t cols[5];
    ui_roll_composite('0', '1', 2, false, cols);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(K2, cols, 5);
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
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ZEROS, d.out.glyphs[6], 5);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ZEROS, d.out.glyphs[7], 5);
}

// ---- TIME lockstep roll ----

static void test_time_second_roll_timeline(void) {
    FsmDriver d;
    assert_line(d.idle(), TIME_LINE);  // boot frame snaps
    d.snap.tm_now.tm_sec = 37;
    // Trigger frame: roll starts at k=0, still showing the old content.
    assert_line(d.idle_us(40000), TIME_LINE);
    TEST_ASSERT_TRUE(d.out.animating);
    // Interior frames: the seconds cell shows CGRAM slot 1 with the
    // '6'->'7' composite for the current step.
    uint8_t expect[5];
    for (int k = 1; k < UI_ROLL_STEPS; k++) {
        assert_line(d.idle_us(40000), "    14:25:3\x01    ");
        ui_roll_composite('6', '7', k, true, expect);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, d.out.glyphs[1], 5);
    }
    // Final frame: pure ASCII again, defaults restored, animation over.
    assert_line(d.idle_us(40000), "    14:25:37    ");
    TEST_ASSERT_FALSE(d.out.animating);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(UI_GLYPHS[0].cols, d.out.glyphs[1], 5);
}

static void test_minute_rollover_rolls_cells_in_lockstep(void) {
    FsmDriver d;
    d.snap.tm_now.tm_sec = 59;
    d.idle();  // boot frame: 14:25:59
    d.snap.tm_now.tm_min = 26;
    d.snap.tm_now.tm_sec = 0;
    d.idle_us(40000);  // trigger frame (old content)
    // Three changed cells (min units, sec tens, sec units) roll together,
    // slots assigned left to right.
    assert_line(d.idle_us(40000), "    14:2\x01:\x02\x03    ");
    d.settle();
    assert_line(d.out, "    14:26:00    ");
}

static void test_over_budget_lockstep_snaps_excess_cells(void) {
    // A 24H format flip changes 10 cells; only 7 CGRAM slots exist, so the
    // three rightmost changed cells snap straight to the target.
    FsmDriver d;
    assert_line(d.idle(), TIME_LINE);
    d.snap.use24h = false;
    d.idle_us(40000);  // trigger
    assert_line(d.idle_us(40000), "   \x01\x02\x03\x04\x05\x06\x07 PM   ");
    d.settle();
    assert_line(d.out, "   2:25:36 PM   ");
}

// ---- page-change wave ----

static void test_page_wave_staggers_left_to_right(void) {
    FsmDriver d;
    d.idle();
    // Trigger frame still shows the old page in full.
    assert_line(d.feed(UiInput::StepCW), TIME_LINE);
    TEST_ASSERT_TRUE(d.out.animating);
    // 40 ms in: only the first changed cell (index 1) has started; later
    // ordinals are still inside their 50 ms stagger.
    assert_line(d.idle_us(40000), " \x01  14:25:36    ");
    // Never more mid-roll cells than CGRAM slots, all the way to the end.
    for (int i = 0; i < 64 && d.out.animating; i++) {
        d.idle_us(40000);
        int rolling = 0;
        for (int j = 0; j < 16; j++)
            if (d.out.line[j] >= 1 && d.out.line[j] <= 7) rolling++;
        TEST_ASSERT_TRUE(rolling <= 7);
    }
    assert_line(d.out, DATE_LINE);
}

static void test_ccw_wave_rolls_downward(void) {
    FsmDriver d;
    d.idle();
    assert_line(d.feed(UiInput::StepCCW), TIME_LINE);  // wraps to PRESSURE
    d.idle_us(40000, 2);  // first changed cell (index 0) at k=2
    TEST_ASSERT_EQUAL_CHAR('\x01', d.out.line[0]);
    uint8_t expect[5];
    ui_roll_composite(' ', 'P', 2, false, expect);  // downward
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, d.out.glyphs[1], 5);
    d.settle();
    assert_line(d.out, PRESSURE_LINE);
}

// ---- interaction with input and overlays ----

static void test_input_mid_roll_snaps_then_processes(void) {
    FsmDriver d;
    d.idle();
    d.feed(UiInput::StepCW);   // TIME -> DATE wave
    d.idle_us(40000, 3);       // mid-flight
    // The step snaps the wave and starts the next one from a clean DATE.
    assert_line(d.feed(UiInput::StepCW), DATE_LINE);
    d.settle();
    assert_line(d.out, INDOOR_LINE);
}

static void test_btndown_mid_roll_cancels_before_hold_bar(void) {
    FsmDriver d;
    d.idle();
    d.feed(UiInput::StepCW);
    d.idle_us(40000);
    assert_line(d.feed(UiInput::BtnDown), DATE_LINE);  // snapped, no overlay yet
    TEST_ASSERT_FALSE(d.out.animating);
    assert_line(d.idle(5), "MENU     [     ]");  // bar at 500 ms
    TEST_ASSERT_TRUE(d.out.animating);           // hold bar animates the fill
}

static void test_portal_banner_suppresses_roll(void) {
    FsmDriver d;
    d.snap.net = UiNetState::Portal;
    d.idle();
    d.feed(UiInput::StepCW);  // page steps underneath the banner
    TEST_ASSERT_FALSE(d.out.animating);
    d.snap.net = UiNetState::Connected;
    // First un-overlaid frame snaps to the stepped-to page — no stale roll.
    assert_line(d.idle(), DATE_LINE);
    TEST_ASSERT_FALSE(d.out.animating);
}

static void test_menu_exit_snaps_to_pages(void) {
    FsmDriver d;
    d.long_press();
    d.idle();  // menu (arrow glyph rides the default channel)
    TEST_ASSERT_EQUAL_UINT8_ARRAY(UI_GLYPHS[UI_GLYPH_ARROW - 1].cols,
                                  d.out.glyphs[UI_GLYPH_ARROW], 5);
    d.feed(UiInput::StepCCW);  // EXIT
    assert_line(d.click(), TIME_LINE);  // immediate, no roll out of the menu
    TEST_ASSERT_FALSE(d.out.animating);
}

static void test_msg_jump_to_custom_rolls(void) {
    FsmDriver d;
    assert_line(d.idle(), TIME_LINE);
    strcpy(d.snap.msg, "HI");
    d.snap.msg_seq++;
    assert_line(d.idle(), TIME_LINE);  // trigger frame: old page
    TEST_ASSERT_TRUE(d.out.animating);
    d.settle();
    assert_line(d.out, CUSTOM_LINE);
}

static void test_custom_auto_advance_rolls(void) {
    FsmDriver d;
    strcpy(d.snap.msg, "HI");
    d.idle();
    for (int i = 0; i < 4; i++) step_settled(d, UiInput::StepCW);
    assert_line(d.out, CUSTOM_LINE);
    d.snap.msg[0] = '\0';  // cleared via the API mid-display
    assert_line(d.idle(), CUSTOM_LINE);  // trigger frame: old content
    TEST_ASSERT_TRUE(d.out.animating);
    d.settle();
    assert_line(d.out, PRESSURE_LINE);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_font_all_printable_bit7_clear);
    RUN_TEST(test_font_out_of_range_is_space);
    RUN_TEST(test_font_spot_checks);
    RUN_TEST(test_composite_endpoints_are_pure_glyphs);
    RUN_TEST(test_composite_upward_mid_frames);
    RUN_TEST(test_composite_downward_mid_frame);
    RUN_TEST(test_idle_tick_carries_default_glyphs);
    RUN_TEST(test_time_second_roll_timeline);
    RUN_TEST(test_minute_rollover_rolls_cells_in_lockstep);
    RUN_TEST(test_over_budget_lockstep_snaps_excess_cells);
    RUN_TEST(test_page_wave_staggers_left_to_right);
    RUN_TEST(test_ccw_wave_rolls_downward);
    RUN_TEST(test_input_mid_roll_snaps_then_processes);
    RUN_TEST(test_btndown_mid_roll_cancels_before_hold_bar);
    RUN_TEST(test_portal_banner_suppresses_roll);
    RUN_TEST(test_menu_exit_snaps_to_pages);
    RUN_TEST(test_msg_jump_to_custom_rolls);
    RUN_TEST(test_custom_auto_advance_rolls);
    return UNITY_END();
}
