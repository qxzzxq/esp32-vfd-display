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
enum { ITEM_BRIGHT, ITEM_WIFIRST, ITEM_EXIT, ITEM_COUNT };

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

static void test_registry_has_three_items(void) {
    uint8_t n = 0;
    ui_menu_items(&n);
    TEST_ASSERT_EQUAL_INT(ITEM_COUNT, n);
}

static void test_bright_render_highlighted(void) {
    UiSnapshot s = make_snapshot();  // bright 128 = menu units 8
    assert_render(ITEM_BRIGHT, false, s, ">BRIGHT        8");
}

static void test_bright_click_seeds_and_enters_edit(void) {
    UiSnapshot s = make_snapshot();
    UiOutput out = make_output();
    TEST_ASSERT_EQUAL_INT((int)MenuItem::ClickResult::EnterEdit,
                          (int)item(ITEM_BRIGHT)->on_click(s, out));
    TEST_ASSERT_EQUAL_INT(0, out.effect_count);
    // ">" cursor moves to the value side while editing
    assert_render(ITEM_BRIGHT, true, s, " BRIGHT       >8");
}

static void test_bright_seed_clamps_zero_to_one(void) {
    // Saved bright 0 seeds the edit at 1; committing without rotating
    // persists 16 (pre-refactor quirk, preserved deliberately).
    UiSnapshot s = make_snapshot();
    s.bright = 0;
    UiOutput out = make_output();
    item(ITEM_BRIGHT)->on_click(s, out);
    assert_render(ITEM_BRIGHT, true, s, " BRIGHT       >1");
    TEST_ASSERT_TRUE(item(ITEM_BRIGHT)->edit_click(s, out));
    assert_single_effect(out, UiEffect::Type::CommitBrightness, 16);
}

static void test_bright_step_changes_value_with_live_preview(void) {
    UiSnapshot s = make_snapshot();
    UiOutput out = make_output();
    item(ITEM_BRIGHT)->on_click(s, out);  // seed 8
    TEST_ASSERT_TRUE(item(ITEM_BRIGHT)->edit_step(1, s, out));
    assert_single_effect(out, UiEffect::Type::SetBrightness, 144);  // 9 * 16
    assert_render(ITEM_BRIGHT, true, s, " BRIGHT       >9");
}

static void test_bright_step_clamps_and_still_previews(void) {
    // The pre-refactor code emits setBrightness even when already clamped.
    UiSnapshot s = make_snapshot();
    s.bright = 240;  // menu units 15
    UiOutput out = make_output();
    item(ITEM_BRIGHT)->on_click(s, out);  // seed 15
    TEST_ASSERT_TRUE(item(ITEM_BRIGHT)->edit_step(1, s, out));
    assert_single_effect(out, UiEffect::Type::SetBrightness, 240);
    assert_render(ITEM_BRIGHT, true, s, " BRIGHT      >15");

    s.bright = 16;  // menu units 1
    out = make_output();
    item(ITEM_BRIGHT)->on_click(s, out);  // seed 1
    TEST_ASSERT_TRUE(item(ITEM_BRIGHT)->edit_step(-1, s, out));
    assert_single_effect(out, UiEffect::Type::SetBrightness, 16);
    assert_render(ITEM_BRIGHT, true, s, " BRIGHT       >1");
}

static void test_bright_commit_persists_edited_value(void) {
    UiSnapshot s = make_snapshot();
    UiOutput out = make_output();
    item(ITEM_BRIGHT)->on_click(s, out);      // seed 8
    item(ITEM_BRIGHT)->edit_step(1, s, out);  // 9
    out = make_output();
    TEST_ASSERT_TRUE(item(ITEM_BRIGHT)->edit_click(s, out));
    assert_single_effect(out, UiEffect::Type::CommitBrightness, 144);
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

static void test_wifi_reset_render(void) {
    UiSnapshot s = make_snapshot();
    assert_render(ITEM_WIFIRST, false, s, ">WIFI RESET     ");
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
    assert_render(ITEM_EXIT, false, s, ">EXIT           ");
    UiOutput out = make_output();
    TEST_ASSERT_EQUAL_INT((int)MenuItem::ClickResult::ExitMenu,
                          (int)item(ITEM_EXIT)->on_click(s, out));
    TEST_ASSERT_EQUAL_INT(0, out.effect_count);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_registry_has_three_items);
    RUN_TEST(test_bright_render_highlighted);
    RUN_TEST(test_bright_click_seeds_and_enters_edit);
    RUN_TEST(test_bright_seed_clamps_zero_to_one);
    RUN_TEST(test_bright_step_changes_value_with_live_preview);
    RUN_TEST(test_bright_step_clamps_and_still_previews);
    RUN_TEST(test_bright_commit_persists_edited_value);
    RUN_TEST(test_bright_abort_restores_saved_brightness);
    RUN_TEST(test_wifi_reset_render);
    RUN_TEST(test_wifi_reset_arm_cancel_confirm);
    RUN_TEST(test_exit_render_and_click);
    return UNITY_END();
}
