// State-machine tests: gesture recognition (click vs long-press with hold
// bar), mode transitions, menu timeout, overlays, and the pre-refactor
// timing quirks the shell swap must preserve. The FSM is observed strictly
// through UiOutput (rendered line + effects) — no state is exposed.
#include <string.h>
#include <unity.h>

#include "../ui_test_helpers.h"

void setUp(void) {}
void tearDown(void) {}

// Golden lines for make_snapshot() (24h, SAT 2026-07-05, bright 128 = 8).
// \x05 is the CGRAM arrow cursor, \x06 the CGRAM solid block (full bar cell),
// \x01..\x04 the partial bar cells (code == lit columns). Hex escapes are
// greedy — a glyph code followed by a hex-digit-ish char needs adjacent-
// literal concatenation ("\x05" "BRIGHT", not "\x05BRIGHT" == "\x5BRIGHT").
static const char* TIME_LINE = "    14:25:36    ";
static const char* DATE_LINE = " SAT 2026-07-05 ";
static const char* INDOOR_LINE = "IN  23.4C   47% ";
static const char* OUTDOOR_LINE = "OUT 31C 60% UV9 ";
static const char* PRESSURE_LINE = "PRES 1013.2 hPa ";
static const char* CUSTOM_LINE = "       HI       ";  // msg "HI", centered
static const char* MENU_BRIGHT = "\x05" "BRIGHT        8";
static const char* EDIT_BRIGHT_8 = " BRIGHT       \x05" "8";
static const char* MENU_24H = "\x05" "24H          ON";  // make_snapshot: 24h on
static const char* MENU_TZ = "\x05" "TZ          UTC";   // tz_idx 0 = UTC (helper)
static const char* MENU_CYCLE = "\x05" "CYCLE       OFF"; // cycle_s 0 = OFF
static const char* MENU_WIFI = "\x05" "WIFI RESET     ";
static const char* MENU_STATUS = "\x05" "STATUS         ";
static const char* MENU_EXIT = "\x05" "EXIT           ";

static void assert_line(const UiOutput& out, const char* expect) {
    TEST_ASSERT_EQUAL_STRING(expect, out.line);
}

static void assert_no_effects(const UiOutput& out) {
    TEST_ASSERT_EQUAL_INT(0, out.effect_count);
}

static void assert_single_effect(const UiOutput& out, UiEffect::Type t, uint8_t arg) {
    TEST_ASSERT_EQUAL_INT(1, out.effect_count);
    TEST_ASSERT_EQUAL_INT((int)t, (int)out.effects[0].type);
    TEST_ASSERT_EQUAL_INT((int)arg, (int)out.effects[0].arg);
}

// Enter the menu from pages via long-press; leaves the FSM settled on ">BRIGHT"
// (the hold bar crossfades into the menu, so settle before asserting).
static void enter_menu(FsmDriver& d) {
    d.long_press();
    d.settle();
    assert_line(d.idle(), MENU_BRIGHT);
}

static void test_boot_renders_first_page(void) {
    FsmDriver d;
    assert_line(d.idle(), TIME_LINE);
    assert_no_effects(d.out);
}

// make_snapshot() leaves msg empty, so this also pins skip-empty-CUSTOM
// (OUTDOOR steps straight to PRESSURE in both directions).
// step() settles the page-change crossfade so the destination page is what's
// asserted (the transition frames are pinned in test_ui_fade).
static void test_step_cycles_pages_and_wraps(void) {
    FsmDriver d;
    assert_line(d.step(UiInput::StepCW), DATE_LINE);
    assert_line(d.step(UiInput::StepCW), INDOOR_LINE);
    assert_line(d.step(UiInput::StepCW), OUTDOOR_LINE);
    assert_line(d.step(UiInput::StepCW), PRESSURE_LINE);
    assert_line(d.step(UiInput::StepCW), TIME_LINE);  // wraps forward
    assert_line(d.step(UiInput::StepCCW), PRESSURE_LINE);  // wraps backward
}

// Empty msg here too: CUSTOM and PRESSURE are skipped back to back.
static void test_step_skips_pressure_without_bmp280(void) {
    FsmDriver d;
    d.snap.has_pressure = false;
    d.step(UiInput::StepCW);  // DATE
    d.step(UiInput::StepCW);  // INDOOR
    d.step(UiInput::StepCW);  // OUTDOOR
    assert_line(d.step(UiInput::StepCW), TIME_LINE);  // skips PRESSURE
    assert_line(d.step(UiInput::StepCCW), OUTDOOR_LINE);  // skips backward too
}

static void test_step_cycles_six_pages_with_message(void) {
    FsmDriver d;
    strcpy(d.snap.msg, "HI");
    assert_line(d.step(UiInput::StepCW), DATE_LINE);
    assert_line(d.step(UiInput::StepCW), INDOOR_LINE);
    assert_line(d.step(UiInput::StepCW), OUTDOOR_LINE);
    assert_line(d.step(UiInput::StepCW), CUSTOM_LINE);  // #5, after OUTDOOR
    assert_line(d.step(UiInput::StepCW), PRESSURE_LINE);
    assert_line(d.step(UiInput::StepCW), TIME_LINE);  // wraps forward
    assert_line(d.step(UiInput::StepCCW), PRESSURE_LINE);
    assert_line(d.step(UiInput::StepCCW), CUSTOM_LINE);  // backward too
}

// A page can lose availability while it is on screen (CUSTOM cleared via
// POST /api/message): the next render tick must advance off it instead of
// showing a stale/blank page.
static void test_custom_cleared_mid_display_auto_advances(void) {
    FsmDriver d;
    strcpy(d.snap.msg, "HI");
    for (int i = 0; i < 4; i++) d.feed(UiInput::StepCW);  // land on CUSTOM
    d.settle();
    assert_line(d.out, CUSTOM_LINE);
    d.snap.msg[0] = '\0';  // cleared via the API mid-display
    d.idle();              // advance off CUSTOM (crossfades to PRESSURE)
    d.settle();
    assert_line(d.out, PRESSURE_LINE);
}

static void test_auto_advance_skips_consecutive_unavailable(void) {
    FsmDriver d;
    strcpy(d.snap.msg, "HI");
    for (int i = 0; i < 4; i++) d.feed(UiInput::StepCW);  // land on CUSTOM
    d.settle();
    d.snap.msg[0] = '\0';
    d.snap.has_pressure = false;  // PRESSURE gone too
    d.idle();
    d.settle();
    assert_line(d.out, TIME_LINE);
}

// A new POST bumps msg_seq; the display jumps to CUSTOM so the pushed
// message actually shows (notification semantics).
static void test_new_message_jumps_to_custom(void) {
    FsmDriver d;
    assert_line(d.idle(), TIME_LINE);
    strcpy(d.snap.msg, "HI");
    d.snap.msg_seq++;  // POST arrived
    d.idle();          // jump to CUSTOM (crossfades)
    d.settle();
    assert_line(d.out, CUSTOM_LINE);
}

static void test_same_seq_does_not_rejump(void) {
    FsmDriver d;
    strcpy(d.snap.msg, "HI");
    d.snap.msg_seq++;
    d.idle();
    d.settle();  // jumped to CUSTOM
    assert_line(d.step(UiInput::StepCW), PRESSURE_LINE);  // rotate away
    assert_line(d.idle(), PRESSURE_LINE);  // stays put: no new POST
}

static void test_clearing_post_does_not_jump(void) {
    FsmDriver d;
    assert_line(d.idle(), TIME_LINE);
    d.snap.msg_seq++;  // POST of "" — seq bumps but msg stays empty
    assert_line(d.idle(), TIME_LINE);
}

static void test_new_message_does_not_interrupt_menu(void) {
    FsmDriver d;
    enter_menu(d);
    strcpy(d.snap.msg, "HI");
    d.snap.msg_seq++;
    assert_line(d.idle(), MENU_BRIGHT);  // menu unaffected
    // The seq is consumed while in the menu: exiting later does not jump.
    d.feed(UiInput::StepCCW);  // EXIT
    d.click();
    assert_line(d.idle(), TIME_LINE);
}

static void test_click_on_pages_is_unassigned(void) {
    FsmDriver d;
    d.idle();
    assert_line(d.click(), TIME_LINE);
    assert_no_effects(d.out);
}

static void test_hold_bar_timeline_and_menu_entry(void) {
    FsmDriver d;
    d.feed(UiInput::BtnDown);
    // Clicks stay visually clean: no bar below 500 ms.
    assert_line(d.idle(4), TIME_LINE);  // held 400 ms
    // At 500 ms the bar crossfades in — the clock dims out first, so the fill
    // only shows at full brightness once the ~200 ms entry fade completes.
    assert_line(d.idle(1), TIME_LINE);  // held 500 ms: fade dims the clock out
    assert_line(d.idle(2), "MENU       \x06\x06   ");  // 700 ms: fade done, 2 cells
    assert_line(d.idle(2), "MENU       \x06\x06\x06\x06 ");  // 900 ms
    // Threshold tick: completed bar is drawn, the fire is signalled for the
    // shell's 200 ms pause, and the menu is entered at item 0.
    d.idle(1);  // held 1.0 s
    assert_line(d.out, "MENU       \x06\x06\x06\x06\x06");
    TEST_ASSERT_TRUE(d.out.hold_fired);
    assert_no_effects(d.out);
    assert_line(d.settle(), MENU_BRIGHT);  // crossfades into the menu
}

static void test_release_after_fire_is_swallowed(void) {
    FsmDriver d;
    d.long_press();  // fires, still held
    d.settle();      // crossfade into the menu
    d.now_us += 300000;
    // The eventual release must NOT click (a click would enter BRIGHT edit).
    assert_line(d.feed(UiInput::BtnUp), MENU_BRIGHT);
    assert_no_effects(d.out);
}

static void test_release_exactly_at_threshold_is_lost(void) {
    // Pre-refactor quirk: BtnUp landing exactly at the 1.0 s threshold before
    // a tick fired means no click AND no fire — the press is lost.
    FsmDriver d;
    d.idle();
    d.feed(UiInput::BtnDown);
    d.idle(9);  // held 0.9 s, not fired yet
    d.now_us += 100000;  // held exactly 1.0 s
    d.feed(UiInput::BtnUp);
    TEST_ASSERT_FALSE(d.out.hold_fired);  // no fire
    assert_line(d.settle(), TIME_LINE);   // crossfades back to the pages
}

static void test_rotation_while_held_still_steps(void) {
    FsmDriver d;
    d.idle();   // valid pages frame so the step below crossfades
    d.feed(UiInput::BtnDown);
    d.idle(3);  // held 300 ms, no bar yet
    // The step registers a page change: it begins a crossfade from the
    // outgoing page (TIME dims out). Settling here would fire the hold, so the
    // dim-out frame is what pins that the rotation was processed.
    assert_line(d.feed(UiInput::StepCW), TIME_LINE);
    TEST_ASSERT_TRUE(d.out.animating);
    // The bar keeps filling from the original press (overriding the fade).
    assert_line(d.idle(4), "MENU       \x06\x06   ");  // held 700 ms
}

// The fill is column-granular: a partial cell renders as a partial CGRAM bar
// glyph (code == lit columns). Sampled after the ~200 ms entry crossfade so the
// bar shows at full brightness rather than mid-fade.
static void test_hold_bar_column_granular(void) {
    FsmDriver d;
    d.feed(UiInput::BtnDown);
    d.now_us += 500000;  // 500 ms: bar appears, entry crossfade starts
    d.feed(UiInput::None);
    d.now_us += 260000;  // 760 ms: fade done; 13 columns = 2 full + a 3-col partial
    assert_line(d.feed(UiInput::None), "MENU       \x06\x06\x03  ");
}

// step() settles the item-to-item crossfade so the destination item is asserted.
// Walks the full M7 registry order: BRIGHT,24H,TZ,CYCLE,WIFI,STATUS,EXIT.
static void test_menu_step_wraps(void) {
    FsmDriver d;
    enter_menu(d);
    assert_line(d.step(UiInput::StepCW), MENU_24H);
    assert_line(d.step(UiInput::StepCW), MENU_TZ);
    assert_line(d.step(UiInput::StepCW), MENU_CYCLE);
    assert_line(d.step(UiInput::StepCW), MENU_WIFI);
    assert_line(d.step(UiInput::StepCW), MENU_STATUS);
    assert_line(d.step(UiInput::StepCW), MENU_EXIT);
    assert_line(d.step(UiInput::StepCW), MENU_BRIGHT);  // wraps forward
    assert_line(d.step(UiInput::StepCCW), MENU_EXIT);   // wraps backward
}

static void test_exit_item_returns_to_pages(void) {
    FsmDriver d;
    enter_menu(d);
    d.step(UiInput::StepCCW);  // EXIT
    d.click();                 // exit crossfades back to the pages
    assert_line(d.settle(), TIME_LINE);
    TEST_ASSERT_FALSE(d.out.animating);
}

// A button edge snaps the menu-item crossfade to full brightness, so a click
// acts on the item now shown (item_), never one still dimming in behind the
// fade — the display can't lag the action.
static void test_button_snaps_menu_item_fade(void) {
    FsmDriver d;
    enter_menu(d);
    assert_line(d.feed(UiInput::StepCW), MENU_BRIGHT);  // rotate: fade shows outgoing BRIGHT
    TEST_ASSERT_TRUE(d.out.animating);
    // Button-down snaps to the incoming item at full brightness before any action.
    assert_line(d.feed(UiInput::BtnDown), MENU_24H);
    TEST_ASSERT_FALSE(d.out.animating);
    assert_single_effect(d.out, UiEffect::Type::SetBrightness, 128);
}

static void test_bright_edit_commit_flow(void) {
    FsmDriver d;
    enter_menu(d);
    assert_line(d.click(), EDIT_BRIGHT_8);  // seed from saved 128
    assert_no_effects(d.out);
    assert_line(d.feed(UiInput::StepCW), " BRIGHT       \x05" "9");
    assert_single_effect(d.out, UiEffect::Type::SetBrightness, 144);  // live preview
    d.click();  // commit
    assert_single_effect(d.out, UiEffect::Type::CommitBrightness, 144);
    // The commit tick must already show the committed value — the persist
    // effect runs after the draw, so the item updates the tick's view
    // (regression: the menu flashed the previous value for one tick).
    assert_line(d.out, "\x05" "BRIGHT        9");
    // Once the shell persists (snapshot catches up), it stays the new value.
    d.snap.bright = 144;
    assert_line(d.idle(), "\x05" "BRIGHT        9");
}

static void test_long_press_from_menu_exits_without_effects(void) {
    FsmDriver d;
    enter_menu(d);
    d.long_press();
    assert_line(d.out, "EXIT       \x06\x06\x06\x06\x06");
    TEST_ASSERT_TRUE(d.out.hold_fired);
    assert_no_effects(d.out);  // nothing to undo from Menu mode
    assert_line(d.settle(), TIME_LINE);  // crossfades back to the pages
}

static void test_long_press_from_edit_restores_brightness(void) {
    FsmDriver d;
    enter_menu(d);
    d.click();                  // BRIGHT edit, seeded 8
    d.feed(UiInput::StepCCW);   // dim to 7, previewed
    d.long_press();
    TEST_ASSERT_TRUE(d.out.hold_fired);
    assert_single_effect(d.out, UiEffect::Type::SetBrightness, 128);  // saved value
    assert_line(d.settle(), TIME_LINE);  // crossfades back to the pages
}

static void test_menu_timeout_anchored_at_button_down(void) {
    // Pre-refactor quirk: the inactivity clock starts at BtnDown, so the
    // menu times out 20 s after the press began — ~19 s after it opened.
    FsmDriver d;
    d.long_press();  // BtnDown at T, menu at T+1.0 s
    assert_line(d.idle(190), MENU_BRIGHT);  // T+20.0 s: '>' comparison, still menu
    d.idle(1);  // T+20.1 s: timed out, crossfades to the pages
    assert_line(d.settle(), TIME_LINE);
}

static void test_menu_timeout_during_edit_aborts(void) {
    FsmDriver d;
    enter_menu(d);
    d.click();                 // edit, seeded 8
    d.feed(UiInput::StepCCW);  // dim to 7 — last input
    d.idle(200);               // 20.0 s later: still editing
    assert_line(d.out, " BRIGHT       \x05" "7");
    d.idle(1);                 // 20.1 s: abandon the edit, crossfade to the pages
    assert_line(d.settle(), TIME_LINE);
}

static void test_wifi_reset_two_step_confirm(void) {
    FsmDriver d;
    enter_menu(d);
    for (int i = 0; i < 4; i++) d.step(UiInput::StepCW);  // BRIGHT -> WIFI (index 4)
    assert_line(d.out, MENU_WIFI);
    assert_line(d.click(), "CLICK = CONFIRM ");  // armed (Menu->Edit, same screen)
    assert_line(d.feed(UiInput::StepCW), MENU_WIFI);  // rotate cancels (same screen)
    assert_no_effects(d.out);
    d.click();  // arm again
    d.click();  // confirm
    assert_single_effect(d.out, UiEffect::Type::WifiReset, 0);
}

static void test_portal_banner_alternates_every_3s(void) {
    FsmDriver d;
    d.snap.net = UiNetState::Portal;
    d.now_us = 5900000;
    assert_line(d.idle(), "SETUP  VFD-ABCD ");  // 6.0 s: even 3 s slot
    d.now_us = 8900000;
    assert_line(d.idle(), "AP 192.168.4.1  ");  // 9.0 s: odd 3 s slot
}

static void test_portal_banner_only_on_pages_and_below_hold_bar(void) {
    FsmDriver d;
    d.snap.net = UiNetState::Portal;
    // The hold bar overrides the banner: holding crossfades it in over the
    // banner (sampled at 700 ms, past the entry fade).
    d.feed(UiInput::BtnDown);
    d.now_us += 500000;
    d.feed(UiInput::None);  // 500 ms: bar begins crossfading in over the banner
    d.now_us += 200000;
    assert_line(d.feed(UiInput::None), "MENU       \x06\x06   ");  // 700 ms: the bar, not "SETUP"
    d.now_us += 300000;
    d.feed(UiInput::None);  // 1.0 s: fires into the menu
    // ...and the menu renders normally with the portal still active.
    assert_line(d.settle(), MENU_BRIGHT);
}

// --- Auto-cycle ---------------------------------------------------------

// With cycle_s set, pages auto-advance once the 30 s post-input pause elapses.
static void test_auto_cycle_advances_after_pause(void) {
    FsmDriver d;
    d.snap.cycle_s = 10;
    assert_line(d.idle(), TIME_LINE);  // establish TIME as the fade-from screen
    d.now_us = 31000000;               // 31 s: past the 30 s pause + one interval
    d.feed(UiInput::None);
    TEST_ASSERT_TRUE(d.out.animating);           // crossfades like a manual step
    assert_line(d.settle(), DATE_LINE);
}

static void test_auto_cycle_off_never_advances(void) {
    FsmDriver d;
    d.snap.cycle_s = 0;  // OFF
    d.idle();
    d.now_us = 120000000;  // 2 min later
    assert_line(d.feed(UiInput::None), TIME_LINE);
    TEST_ASSERT_FALSE(d.out.animating);
}

// Any input restarts the pause, so a recent manual step suppresses auto-advance
// for 30 s before cycling resumes.
static void test_auto_cycle_paused_by_input(void) {
    FsmDriver d;
    d.snap.cycle_s = 10;
    d.idle();
    d.now_us = 25000000;
    d.step(UiInput::StepCW);  // manual -> DATE; resets the pause at 25 s
    d.now_us = 30000000;      // only 5 s since the input: still paused
    assert_line(d.feed(UiInput::None), DATE_LINE);
    TEST_ASSERT_FALSE(d.out.animating);
    d.now_us = 56000000;      // 31 s since the input: resumes, DATE -> INDOOR
    d.feed(UiInput::None);
    assert_line(d.settle(), INDOOR_LINE);
}

// Auto-advance skips unavailable pages just like manual navigation: with no
// custom message, OUTDOOR cycles straight to PRESSURE.
static void test_auto_cycle_skips_empty_custom(void) {
    FsmDriver d;
    d.snap.cycle_s = 5;  // msg empty by default -> CUSTOM unavailable
    d.step(UiInput::StepCW);  // DATE
    d.step(UiInput::StepCW);  // INDOOR
    d.step(UiInput::StepCW);  // OUTDOOR
    d.now_us += 40000000;     // past the pause
    d.feed(UiInput::None);    // OUTDOOR -> (skip CUSTOM) -> PRESSURE
    assert_line(d.settle(), PRESSURE_LINE);
}

// --- STATUS overlay -----------------------------------------------------

static void goto_status(FsmDriver& d) {
    enter_menu(d);  // BRIGHT
    for (int i = 0; i < 5; i++) d.step(UiInput::StepCW);  // -> STATUS (index 5)
    assert_line(d.out, MENU_STATUS);
}

// Click opens the read-only status line; it auto-returns to the menu item after
// UI_STATUS_SHOW_US with nothing persisted.
static void test_status_overlay_returns_after_timeout(void) {
    FsmDriver d;
    goto_status(d);
    strcpy(d.snap.ip, "10.0.0.5");  // net Connected by default
    assert_line(d.click(), "10.0.0.5        ");  // overlay (same screen id: snaps)
    assert_no_effects(d.out);
    d.now_us += 3100000;  // past the 3 s auto-return
    assert_line(d.feed(UiInput::None), MENU_STATUS);
}

static void test_status_overlay_dismissed_by_rotation(void) {
    FsmDriver d;
    goto_status(d);
    d.click();  // open the overlay
    assert_line(d.feed(UiInput::StepCW), MENU_STATUS);  // any rotation dismisses it
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_boot_renders_first_page);
    RUN_TEST(test_step_cycles_pages_and_wraps);
    RUN_TEST(test_step_skips_pressure_without_bmp280);
    RUN_TEST(test_step_cycles_six_pages_with_message);
    RUN_TEST(test_custom_cleared_mid_display_auto_advances);
    RUN_TEST(test_auto_advance_skips_consecutive_unavailable);
    RUN_TEST(test_new_message_jumps_to_custom);
    RUN_TEST(test_same_seq_does_not_rejump);
    RUN_TEST(test_clearing_post_does_not_jump);
    RUN_TEST(test_new_message_does_not_interrupt_menu);
    RUN_TEST(test_click_on_pages_is_unassigned);
    RUN_TEST(test_hold_bar_timeline_and_menu_entry);
    RUN_TEST(test_release_after_fire_is_swallowed);
    RUN_TEST(test_release_exactly_at_threshold_is_lost);
    RUN_TEST(test_rotation_while_held_still_steps);
    RUN_TEST(test_hold_bar_column_granular);
    RUN_TEST(test_menu_step_wraps);
    RUN_TEST(test_exit_item_returns_to_pages);
    RUN_TEST(test_button_snaps_menu_item_fade);
    RUN_TEST(test_bright_edit_commit_flow);
    RUN_TEST(test_long_press_from_menu_exits_without_effects);
    RUN_TEST(test_long_press_from_edit_restores_brightness);
    RUN_TEST(test_menu_timeout_anchored_at_button_down);
    RUN_TEST(test_menu_timeout_during_edit_aborts);
    RUN_TEST(test_wifi_reset_two_step_confirm);
    RUN_TEST(test_portal_banner_alternates_every_3s);
    RUN_TEST(test_portal_banner_only_on_pages_and_below_hold_bar);
    RUN_TEST(test_auto_cycle_advances_after_pause);
    RUN_TEST(test_auto_cycle_off_never_advances);
    RUN_TEST(test_auto_cycle_paused_by_input);
    RUN_TEST(test_auto_cycle_skips_empty_custom);
    RUN_TEST(test_status_overlay_returns_after_timeout);
    RUN_TEST(test_status_overlay_dismissed_by_rotation);
    return UNITY_END();
}
