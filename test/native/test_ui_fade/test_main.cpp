// Page-transition crossfade tests: a page change dims the outgoing page to
// black over UI_FADE_HALF_US (Out), swaps DCRAM content at black, then dims the
// incoming page back in over another UI_FADE_HALF_US (In), restoring the saved
// brightness. Out always completes; a page change mid-Out only retargets, while
// one mid-In restarts In from black for the new page. Driven by SetBrightness
// effects; yields to overlays (hold bar / portal / menu). The time-based fade
// envelope is sampled here in 40 ms steps via FsmDriver::idle_us/settle.
#include <string.h>
#include <unity.h>

#include "../ui_test_helpers.h"

void setUp(void) {}
void tearDown(void) {}

static const char* TIME_LINE = "    14:25:36    ";
static const char* DATE_LINE = " SAT 2026-07-05 ";
static const char* INDOOR_LINE = "IN  23.4C   47% ";
static const char* CUSTOM_LINE = "       HI       ";
static const char* PRESSURE_LINE = "PRES 1013.2 hPa ";

static void assert_line(const UiOutput& out, const char* expect) {
    TEST_ASSERT_EQUAL_STRING(expect, out.line);
}

// The brightness the tick asked the shell to apply, or -1 if none this tick.
static int brightness(const UiOutput& out) {
    int b = -1;
    for (int i = 0; i < out.effect_count; i++)
        if (out.effects[i].type == UiEffect::Type::SetBrightness)
            b = out.effects[i].arg;
    return b;
}

// A step from TIME->DATE plays the full envelope: the outgoing page is held
// through the dim-out half (7 frames strictly under 300 ms at 40 ms each), the
// incoming page appears for the dim-in half, and the saved brightness (128) is
// restored on the settle frame that also clears `animating`.
static void test_page_change_crossfade_timeline(void) {
    FsmDriver d;
    d.idle();  // settle on TIME so prev_page_ is valid
    // Trigger: the outgoing page is still on screen, at the saved brightness.
    assert_line(d.feed(UiInput::StepCW), TIME_LINE);
    TEST_ASSERT_TRUE(d.out.animating);
    TEST_ASSERT_EQUAL_INT(128, brightness(d.out));
    // Dim-out half: outgoing page held, brightness strictly falls.
    int prev = 128;
    for (int i = 0; i < 7; i++) {
        d.idle_us(40000);
        assert_line(d.out, TIME_LINE);
        TEST_ASSERT_TRUE(d.out.animating);
        int b = brightness(d.out);
        TEST_ASSERT_TRUE(b < prev);
        prev = b;
    }
    // Dim-in half: incoming page shown, brightness strictly climbs.
    prev = -1;
    for (int i = 0; i < 7; i++) {
        d.idle_us(40000);
        assert_line(d.out, DATE_LINE);
        TEST_ASSERT_TRUE(d.out.animating);
        int b = brightness(d.out);
        TEST_ASSERT_TRUE(b > prev);
        prev = b;
    }
    // Settle frame: brightness restored to the saved level, animation over.
    d.idle_us(40000);
    assert_line(d.out, DATE_LINE);
    TEST_ASSERT_FALSE(d.out.animating);
    TEST_ASSERT_EQUAL_INT(128, brightness(d.out));
}

// The dark midpoint is where the content swaps: the outgoing page is the last
// thing shown before the swap and the incoming page the first after, both at
// the fade's lowest brightness.
static void test_content_swaps_at_dark_midpoint(void) {
    FsmDriver d;
    d.idle();
    d.feed(UiInput::StepCW);  // TIME -> DATE
    // The swap straddles the dark midpoint: the last outgoing frame and the
    // first incoming frame are both near black.
    d.idle_us(40000, 7);      // elapsed 280 ms: last dim-out frame
    assert_line(d.out, TIME_LINE);
    TEST_ASSERT_TRUE(brightness(d.out) < 20);
    d.idle_us(40000, 1);      // elapsed 320 ms: first dim-in frame
    assert_line(d.out, DATE_LINE);
    TEST_ASSERT_TRUE(brightness(d.out) < 20);
}

// A page change during dim-out only retargets: the outgoing page keeps dimming
// out (brightness never snaps back to full), and the latest target dims in once
// black is reached.
static void test_step_during_dim_out_retargets_not_restarts(void) {
    FsmDriver d;
    d.idle();                 // settled on TIME
    d.feed(UiInput::StepCW);  // TIME -> DATE: dim-out of TIME begins
    d.idle_us(40000, 4);      // 160 ms into the dim-out
    assert_line(d.out, TIME_LINE);
    int mid = brightness(d.out);
    TEST_ASSERT_TRUE(mid < 128 && mid > 0);
    // Step again mid dim-out: retargets to INDOOR without restarting.
    d.feed(UiInput::StepCW);  // DATE -> INDOOR
    assert_line(d.out, TIME_LINE);                 // still the original page, dimming
    TEST_ASSERT_TRUE(brightness(d.out) <= mid);    // kept falling, not reset to full
    // Dim-out completes, then the latest target (INDOOR) dims in.
    d.settle();
    assert_line(d.out, INDOOR_LINE);
    TEST_ASSERT_EQUAL_INT(128, brightness(d.out));
}

// A page change during dim-in interrupts it: the new page appears from black
// (brightness 0) and dims in — the previous page never snaps back to full.
static void test_step_during_dim_in_restarts_from_black(void) {
    FsmDriver d;
    d.idle();
    d.feed(UiInput::StepCW);  // TIME -> DATE
    d.idle_us(40000, 10);     // ~400 ms: past dim-out, into DATE's dim-in
    assert_line(d.out, DATE_LINE);
    TEST_ASSERT_TRUE(brightness(d.out) > 0);
    // Step during dim-in: INDOOR takes over from black.
    d.feed(UiInput::StepCW);  // DATE -> INDOOR
    assert_line(d.out, INDOOR_LINE);
    TEST_ASSERT_EQUAL_INT(0, brightness(d.out));
    d.settle();
    assert_line(d.out, INDOOR_LINE);
    TEST_ASSERT_EQUAL_INT(128, brightness(d.out));
}

// The fade scales the saved brightness, not a hardcoded maximum: with the
// setting at 64 the dim-out starts at 64 and the restore lands on 64.
static void test_fade_scales_to_saved_brightness(void) {
    FsmDriver d;
    d.snap.bright = 64;
    d.idle();
    assert_line(d.feed(UiInput::StepCW), TIME_LINE);
    TEST_ASSERT_EQUAL_INT(64, brightness(d.out));
    d.settle();
    assert_line(d.out, DATE_LINE);
    TEST_ASSERT_EQUAL_INT(64, brightness(d.out));
}

// A pushed message jumps to CUSTOM through the same crossfade: the outgoing
// page dims out and CUSTOM dims in.
static void test_msg_jump_crossfades(void) {
    FsmDriver d;
    assert_line(d.idle(), TIME_LINE);
    strcpy(d.snap.msg, "HI");
    d.snap.msg_seq++;
    d.idle();  // jump to CUSTOM starts the fade
    TEST_ASSERT_TRUE(d.out.animating);
    assert_line(d.out, TIME_LINE);  // outgoing page during dim-out
    d.settle();
    assert_line(d.out, CUSTOM_LINE);
    TEST_ASSERT_EQUAL_INT(128, brightness(d.out));
}

// Losing the current page's availability (CUSTOM cleared mid-display) advances
// to the next page through the crossfade too.
static void test_auto_advance_crossfades(void) {
    FsmDriver d;
    strcpy(d.snap.msg, "HI");
    d.idle();
    for (int i = 0; i < 4; i++) d.feed(UiInput::StepCW);  // navigate to CUSTOM
    d.settle();
    assert_line(d.out, CUSTOM_LINE);
    d.snap.msg[0] = '\0';  // cleared via the API mid-display
    d.idle();              // CUSTOM unavailable -> advance to PRESSURE, fading
    TEST_ASSERT_TRUE(d.out.animating);
    d.settle();
    assert_line(d.out, PRESSURE_LINE);
}

// An overlay taking over mid-fade (here the hold bar at 500 ms) ends the fade
// and restores brightness so the overlay never renders dim.
static void test_overlay_ends_fade_and_restores_brightness(void) {
    FsmDriver d;
    d.idle();
    d.feed(UiInput::StepCW);  // start fade TIME -> DATE
    d.idle_us(40000, 3);      // dim-out in progress
    TEST_ASSERT_TRUE(brightness(d.out) < 128);
    d.feed(UiInput::BtnDown);
    d.idle(5);  // held 500 ms: the hold bar overlays the fade
    assert_line(d.out, "MENU     [     ]");
    TEST_ASSERT_EQUAL_INT(128, brightness(d.out));
}

// Returning to the pages from the menu snaps (prev_page_ was invalidated while
// overlaid) — no fade from stale content.
static void test_menu_exit_snaps_no_fade(void) {
    FsmDriver d;
    d.long_press();            // into the menu
    d.feed(UiInput::StepCCW);  // EXIT
    assert_line(d.click(), TIME_LINE);
    TEST_ASSERT_FALSE(d.out.animating);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_page_change_crossfade_timeline);
    RUN_TEST(test_content_swaps_at_dark_midpoint);
    RUN_TEST(test_step_during_dim_out_retargets_not_restarts);
    RUN_TEST(test_step_during_dim_in_restarts_from_black);
    RUN_TEST(test_fade_scales_to_saved_brightness);
    RUN_TEST(test_msg_jump_crossfades);
    RUN_TEST(test_auto_advance_crossfades);
    RUN_TEST(test_overlay_ends_fade_and_restores_brightness);
    RUN_TEST(test_menu_exit_snaps_no_fade);
    return UNITY_END();
}
