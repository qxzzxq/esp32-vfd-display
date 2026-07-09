// Behavior + golden-string tests for the menu items. Expected lines and
// effects are transcribed from the pre-refactor src/ui.cpp (render()'s menu
// switch, handle_click, handle_step) so the shell swap is provably identical.
#include <string.h>
#include <unity.h>

#include "../ui_test_helpers.h"
#include "ui/menu_items.h"

void setUp(void) {}
void tearDown(void) {}

// Registry order, fixed by ui_menu_items().
enum {
    ITEM_BRIGHT,
    ITEM_24H,
    ITEM_TZ,
    ITEM_CYCLE,
    ITEM_WIFIRST,
    ITEM_STATUS,
    ITEM_EXIT,
    ITEM_COUNT
};

static MenuItem* item(int idx) {
    uint8_t n = 0;
    MenuItem* const* items = ui_menu_items(&n);
    TEST_ASSERT_TRUE(idx < n);
    return items[idx];
}

static void assert_render(int idx, bool editing, const UiSnapshot& s, const char* expect) {
    char line[17];
    memset(line, '#', sizeof(line));
    item(idx)->render(line, editing, s);
    TEST_ASSERT_EQUAL_INT(16, (int)strlen(line));
    TEST_ASSERT_EQUAL_STRING(expect, line);
}

static void assert_single_effect(const UiOutput& out, UiEffect::Type t, uint8_t arg) {
    TEST_ASSERT_EQUAL_INT(1, out.effect_count);
    TEST_ASSERT_EQUAL_INT((int)t, (int)out.effects[0].type);
    TEST_ASSERT_EQUAL_INT((int)arg, (int)out.effects[0].arg);
}

static void test_registry_has_all_items(void) {
    uint8_t n = 0;
    ui_menu_items(&n);
    TEST_ASSERT_EQUAL_INT(ITEM_COUNT, n);  // BRIGHT,24H,TZ,CYCLE,WIFI,STATUS,EXIT
}

// \x05 is the CGRAM arrow cursor. Hex escapes are greedy, so a code followed
// by a hex-digit-ish char needs adjacent-literal concatenation ("\x05" "BRIGHT").
static void test_bright_render_highlighted(void) {
    UiSnapshot s = make_snapshot();  // bright 128 = menu units 8
    assert_render(ITEM_BRIGHT, false, s, "\x05" "BRIGHT        8");
}

static void test_bright_click_seeds_and_enters_edit(void) {
    UiSnapshot s = make_snapshot();
    UiOutput out = make_output();
    TEST_ASSERT_EQUAL_INT((int)MenuItem::ClickResult::EnterEdit,
                          (int)item(ITEM_BRIGHT)->on_click(s, out));
    TEST_ASSERT_EQUAL_INT(0, out.effect_count);
    // arrow cursor moves to the value side while editing
    assert_render(ITEM_BRIGHT, true, s, " BRIGHT       \x05" "8");
}

static void test_bright_seed_clamps_zero_to_one(void) {
    // Saved bright 0 seeds the edit at 1; committing without rotating
    // persists 16 (pre-refactor quirk, preserved deliberately).
    UiSnapshot s = make_snapshot();
    s.bright = 0;
    UiOutput out = make_output();
    item(ITEM_BRIGHT)->on_click(s, out);
    assert_render(ITEM_BRIGHT, true, s, " BRIGHT       \x05" "1");
    TEST_ASSERT_TRUE(item(ITEM_BRIGHT)->edit_click(s, out));
    assert_single_effect(out, UiEffect::Type::CommitBrightness, 16);
}

static void test_bright_step_changes_value_with_live_preview(void) {
    UiSnapshot s = make_snapshot();
    UiOutput out = make_output();
    item(ITEM_BRIGHT)->on_click(s, out);  // seed 8
    TEST_ASSERT_TRUE(item(ITEM_BRIGHT)->edit_step(1, s, out));
    assert_single_effect(out, UiEffect::Type::SetBrightness, 144);  // 9 * 16
    assert_render(ITEM_BRIGHT, true, s, " BRIGHT       \x05" "9");
}

static void test_bright_step_clamps_and_still_previews(void) {
    // The pre-refactor code emits setBrightness even when already clamped.
    UiSnapshot s = make_snapshot();
    s.bright = 240;  // menu units 15
    UiOutput out = make_output();
    item(ITEM_BRIGHT)->on_click(s, out);  // seed 15
    TEST_ASSERT_TRUE(item(ITEM_BRIGHT)->edit_step(1, s, out));
    assert_single_effect(out, UiEffect::Type::SetBrightness, 240);
    assert_render(ITEM_BRIGHT, true, s, " BRIGHT      \x05" "15");

    s.bright = 16;  // menu units 1
    out = make_output();
    item(ITEM_BRIGHT)->on_click(s, out);  // seed 1
    TEST_ASSERT_TRUE(item(ITEM_BRIGHT)->edit_step(-1, s, out));
    assert_single_effect(out, UiEffect::Type::SetBrightness, 16);
    assert_render(ITEM_BRIGHT, true, s, " BRIGHT       \x05" "1");
}

static void test_bright_commit_persists_edited_value(void) {
    UiSnapshot s = make_snapshot();
    UiOutput out = make_output();
    item(ITEM_BRIGHT)->on_click(s, out);      // seed 8
    item(ITEM_BRIGHT)->edit_step(1, s, out);  // 9
    out = make_output();
    TEST_ASSERT_TRUE(item(ITEM_BRIGHT)->edit_click(s, out));
    assert_single_effect(out, UiEffect::Type::CommitBrightness, 144);
    // The commit also updates the view so the same tick renders the new
    // value (the persist effect only executes after the draw).
    TEST_ASSERT_EQUAL_INT(144, s.bright);
    assert_render(ITEM_BRIGHT, false, s, "\x05" "BRIGHT        9");
}

static void test_bright_abort_restores_saved_brightness(void) {
    UiSnapshot s = make_snapshot();
    UiOutput out = make_output();
    item(ITEM_BRIGHT)->on_click(s, out);       // seed 8
    item(ITEM_BRIGHT)->edit_step(-1, s, out);  // dimmed to 7, previewed
    out = make_output();
    item(ITEM_BRIGHT)->edit_abort(s, out);
    assert_single_effect(out, UiEffect::Type::SetBrightness, 128);
}

// --- 24H toggle ---------------------------------------------------------
static void test_use24h_render_highlighted(void) {
    UiSnapshot s = make_snapshot();
    s.use24h = true;
    assert_render(ITEM_24H, false, s, "\x05" "24H          ON");
    s.use24h = false;
    assert_render(ITEM_24H, false, s, "\x05" "24H         OFF");
}

static void test_use24h_toggle_and_commit(void) {
    UiSnapshot s = make_snapshot();
    s.use24h = true;
    UiOutput out = make_output();
    TEST_ASSERT_EQUAL_INT((int)MenuItem::ClickResult::EnterEdit,
                          (int)item(ITEM_24H)->on_click(s, out));
    // rotate flips the value; the arrow cursor moves to the value side
    TEST_ASSERT_TRUE(item(ITEM_24H)->edit_step(1, s, out));
    assert_render(ITEM_24H, true, s, " 24H        \x05" "OFF");
    TEST_ASSERT_TRUE(item(ITEM_24H)->edit_click(s, out));
    assert_single_effect(out, UiEffect::Type::CommitUse24h, 0);
    TEST_ASSERT_EQUAL_INT(0, s.use24h);  // view updated for the commit tick
    assert_render(ITEM_24H, false, s, "\x05" "24H         OFF");
}

// --- TZ selection -------------------------------------------------------
static void test_tz_render_highlighted(void) {
    UiSnapshot s = make_snapshot();  // tz_idx 0 = UTC (helper table)
    assert_render(ITEM_TZ, false, s, "\x05" "TZ          UTC");
}

static void test_tz_edit_step_commit(void) {
    UiSnapshot s = make_snapshot();  // UTC / PARIS / TOKYO
    UiOutput out = make_output();
    item(ITEM_TZ)->on_click(s, out);  // seed idx 0
    TEST_ASSERT_TRUE(item(ITEM_TZ)->edit_step(1, s, out));  // -> PARIS
    assert_render(ITEM_TZ, true, s, " TZ       \x05" "PARIS");
    TEST_ASSERT_TRUE(item(ITEM_TZ)->edit_click(s, out));
    assert_single_effect(out, UiEffect::Type::CommitTz, 1);
    TEST_ASSERT_EQUAL_INT(1, s.tz_idx);
    assert_render(ITEM_TZ, false, s, "\x05" "TZ        PARIS");
}

static void test_tz_edit_wraps_backward(void) {
    UiSnapshot s = make_snapshot();
    UiOutput out = make_output();
    item(ITEM_TZ)->on_click(s, out);  // seed idx 0 (UTC)
    TEST_ASSERT_TRUE(item(ITEM_TZ)->edit_step(-1, s, out));  // wraps to TOKYO (idx 2)
    assert_render(ITEM_TZ, true, s, " TZ       \x05" "TOKYO");
}

// --- CYCLE interval -----------------------------------------------------
static void test_cycle_render_highlighted(void) {
    UiSnapshot s = make_snapshot();
    s.cycle_s = 0;
    assert_render(ITEM_CYCLE, false, s, "\x05" "CYCLE       OFF");
    s.cycle_s = 10;
    assert_render(ITEM_CYCLE, false, s, "\x05" "CYCLE       10s");
}

static void test_cycle_edit_step_commit(void) {
    UiSnapshot s = make_snapshot();
    s.cycle_s = 0;
    UiOutput out = make_output();
    item(ITEM_CYCLE)->on_click(s, out);  // seed OFF (idx 0)
    TEST_ASSERT_TRUE(item(ITEM_CYCLE)->edit_step(1, s, out));  // -> 5s
    assert_render(ITEM_CYCLE, true, s, " CYCLE       \x05" "5s");
    TEST_ASSERT_TRUE(item(ITEM_CYCLE)->edit_step(1, s, out));  // -> 10s
    assert_render(ITEM_CYCLE, true, s, " CYCLE      \x05" "10s");
    TEST_ASSERT_TRUE(item(ITEM_CYCLE)->edit_click(s, out));
    assert_single_effect(out, UiEffect::Type::CommitCycle, 10);
    TEST_ASSERT_EQUAL_INT(10, s.cycle_s);
    assert_render(ITEM_CYCLE, false, s, "\x05" "CYCLE       10s");
}

static void test_cycle_edit_wraps_backward(void) {
    UiSnapshot s = make_snapshot();
    s.cycle_s = 0;
    UiOutput out = make_output();
    item(ITEM_CYCLE)->on_click(s, out);  // seed OFF (idx 0)
    TEST_ASSERT_TRUE(item(ITEM_CYCLE)->edit_step(-1, s, out));  // wraps to 60s (idx 4)
    assert_render(ITEM_CYCLE, true, s, " CYCLE      \x05" "60s");
}

// --- STATUS overlay -----------------------------------------------------
static void test_status_render_highlighted(void) {
    UiSnapshot s = make_snapshot();
    assert_render(ITEM_STATUS, false, s, "\x05" "STATUS         ");
}

static void test_status_edit_shows_ip_or_state(void) {
    UiSnapshot s = make_snapshot();
    s.net = UiNetState::Connected;
    strcpy(s.ip, "192.168.1.42");
    assert_render(ITEM_STATUS, true, s, "192.168.1.42    ");
    s.ip[0] = '\0';
    assert_render(ITEM_STATUS, true, s, "NO IP           ");
    s.net = UiNetState::Connecting;
    assert_render(ITEM_STATUS, true, s, "WIFI CONNECTING ");
    s.net = UiNetState::Portal;
    assert_render(ITEM_STATUS, true, s, "AP 192.168.4.1  ");
}

static void test_status_click_and_dismiss(void) {
    UiSnapshot s = make_snapshot();
    UiOutput out = make_output();
    TEST_ASSERT_EQUAL_INT((int)MenuItem::ClickResult::EnterEdit,
                          (int)item(ITEM_STATUS)->on_click(s, out));
    TEST_ASSERT_EQUAL_INT(0, out.effect_count);
    // Any rotation dismisses (false = leave edit); a click also dismisses.
    TEST_ASSERT_FALSE(item(ITEM_STATUS)->edit_step(1, s, out));
    TEST_ASSERT_TRUE(item(ITEM_STATUS)->edit_click(s, out));
    TEST_ASSERT_EQUAL_INT(0, out.effect_count);  // read-only: never persists
    TEST_ASSERT_EQUAL_INT((int)UI_STATUS_SHOW_US,
                          (int)item(ITEM_STATUS)->edit_timeout_us());
}

static void test_wifi_reset_render(void) {
    UiSnapshot s = make_snapshot();
    assert_render(ITEM_WIFIRST, false, s, "\x05" "WIFI RESET     ");
    assert_render(ITEM_WIFIRST, true, s, "CLICK = CONFIRM ");
}

static void test_wifi_reset_arm_cancel_confirm(void) {
    UiSnapshot s = make_snapshot();
    UiOutput out = make_output();
    // Click arms the confirm...
    TEST_ASSERT_EQUAL_INT((int)MenuItem::ClickResult::EnterEdit,
                          (int)item(ITEM_WIFIRST)->on_click(s, out));
    TEST_ASSERT_EQUAL_INT(0, out.effect_count);
    // ...rotating cancels it with no side effects...
    TEST_ASSERT_FALSE(item(ITEM_WIFIRST)->edit_step(1, s, out));
    TEST_ASSERT_EQUAL_INT(0, out.effect_count);
    // ...a second click fires the reset.
    TEST_ASSERT_TRUE(item(ITEM_WIFIRST)->edit_click(s, out));
    assert_single_effect(out, UiEffect::Type::WifiReset, 0);
}

static void test_exit_render_and_click(void) {
    UiSnapshot s = make_snapshot();
    assert_render(ITEM_EXIT, false, s, "\x05" "EXIT           ");
    UiOutput out = make_output();
    TEST_ASSERT_EQUAL_INT((int)MenuItem::ClickResult::ExitMenu,
                          (int)item(ITEM_EXIT)->on_click(s, out));
    TEST_ASSERT_EQUAL_INT(0, out.effect_count);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_registry_has_all_items);
    RUN_TEST(test_bright_render_highlighted);
    RUN_TEST(test_bright_click_seeds_and_enters_edit);
    RUN_TEST(test_bright_seed_clamps_zero_to_one);
    RUN_TEST(test_bright_step_changes_value_with_live_preview);
    RUN_TEST(test_bright_step_clamps_and_still_previews);
    RUN_TEST(test_bright_commit_persists_edited_value);
    RUN_TEST(test_bright_abort_restores_saved_brightness);
    RUN_TEST(test_use24h_render_highlighted);
    RUN_TEST(test_use24h_toggle_and_commit);
    RUN_TEST(test_tz_render_highlighted);
    RUN_TEST(test_tz_edit_step_commit);
    RUN_TEST(test_tz_edit_wraps_backward);
    RUN_TEST(test_cycle_render_highlighted);
    RUN_TEST(test_cycle_edit_step_commit);
    RUN_TEST(test_cycle_edit_wraps_backward);
    RUN_TEST(test_status_render_highlighted);
    RUN_TEST(test_status_edit_shows_ip_or_state);
    RUN_TEST(test_status_click_and_dismiss);
    RUN_TEST(test_wifi_reset_render);
    RUN_TEST(test_wifi_reset_arm_cancel_confirm);
    RUN_TEST(test_exit_render_and_click);
    return UNITY_END();
}
