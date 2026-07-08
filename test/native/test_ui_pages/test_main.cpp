// Golden-string tests for the page renderers. The expected lines are
// transcribed from the pre-refactor firmware (src/ui.cpp render_* functions)
// so the shell swap is provably behavior-preserving.
#include <math.h>
#include <string.h>
#include <unity.h>

#include "../ui_test_helpers.h"
#include "ui/pages.h"

void setUp(void) {}
void tearDown(void) {}

// Registry order, fixed by ui_pages().
enum { PAGE_TIME, PAGE_DATE, PAGE_INDOOR, PAGE_OUTDOOR, PAGE_CUSTOM, PAGE_PRESSURE, PAGE_COUNT };

static const UiPage* page(int idx) {
    uint8_t n = 0;
    UiPage* const* pages = ui_pages(&n);
    TEST_ASSERT_TRUE(idx < n);
    return pages[idx];
}

static void assert_render_at(int idx, const UiSnapshot& s, int64_t now_us, const char* expect) {
    char line[17];
    memset(line, '#', sizeof(line));  // catch missing NUL / short writes
    page(idx)->render(line, s, now_us);
    TEST_ASSERT_EQUAL_INT(16, (int)strlen(line));
    TEST_ASSERT_EQUAL_STRING(expect, line);
}

static void assert_render(int idx, const UiSnapshot& s, const char* expect) {
    assert_render_at(idx, s, 0, expect);
}

static void test_registry_has_six_pages(void) {
    uint8_t n = 0;
    ui_pages(&n);
    TEST_ASSERT_EQUAL_INT(PAGE_COUNT, n);
}

static void test_time_24h(void) {
    UiSnapshot s = make_snapshot();
    assert_render(PAGE_TIME, s, "    14:25:36    ");
}

static void test_time_12h_afternoon(void) {
    UiSnapshot s = make_snapshot();
    s.use24h = false;
    assert_render(PAGE_TIME, s, "   2:25:36 PM   ");
}

static void test_time_12h_midnight(void) {
    UiSnapshot s = make_snapshot();
    s.use24h = false;
    s.tm_now.tm_hour = 0;
    assert_render(PAGE_TIME, s, "  12:25:36 AM   ");
}

static void test_time_12h_noon(void) {
    UiSnapshot s = make_snapshot();
    s.use24h = false;
    s.tm_now.tm_hour = 12;
    assert_render(PAGE_TIME, s, "  12:25:36 PM   ");
}

static void test_time_not_synced(void) {
    UiSnapshot s = make_snapshot();
    s.time_valid = false;
    assert_render(PAGE_TIME, s, "    --:--:--    ");
}

static void test_date(void) {
    UiSnapshot s = make_snapshot();
    assert_render(PAGE_DATE, s, " SAT 2026-07-05 ");
}

static void test_date_not_synced(void) {
    UiSnapshot s = make_snapshot();
    s.time_valid = false;
    assert_render(PAGE_DATE, s, " NO TIME SYNC   ");
}

static void test_indoor(void) {
    UiSnapshot s = make_snapshot();
    assert_render(PAGE_INDOOR, s, "IN  23.4C   47% ");
}

static void test_indoor_sensor_err(void) {
    UiSnapshot s = make_snapshot();
    s.sensors_ok = false;
    assert_render(PAGE_INDOOR, s, "IN  SENSOR ERR  ");
}

// OUTDOOR golden strings match the docs/DESIGN.md UI table; values are
// rounded to integers (%.0f) and the line is left-padded to 16.
static void test_outdoor(void) {
    UiSnapshot s = make_snapshot();
    assert_render(PAGE_OUTDOOR, s, "OUT 31C 60% UV9 ");
}

static void test_outdoor_negative_temp_rounds(void) {
    UiSnapshot s = make_snapshot();
    s.out_tC = -5.4f;
    s.out_rh = 80.4f;
    s.out_uv = 0.2f;
    assert_render(PAGE_OUTDOOR, s, "OUT -5C 80% UV0 ");
}

static void test_outdoor_widest_values_fill_line(void) {
    // -10C + 100% + UV11 is the widest realistic combination: exactly 16.
    UiSnapshot s = make_snapshot();
    s.out_tC = -10.2f;
    s.out_rh = 99.6f;
    s.out_uv = 10.7f;
    assert_render(PAGE_OUTDOOR, s, "OUT-10C100% UV11");
}

static void test_outdoor_stale_after_45_min(void) {
    UiSnapshot s = make_snapshot();
    s.weather_age_s = 45 * 60;  // exactly 45 min: still fresh
    assert_render(PAGE_OUTDOOR, s, "OUT 31C 60% UV9 ");
    s.weather_age_s = 45 * 60 + 1;
    assert_render(PAGE_OUTDOOR, s, "OUT 31C 60% UV9?");
}

static void test_outdoor_no_data(void) {
    UiSnapshot s = make_snapshot();
    s.weather_ok = false;
    assert_render(PAGE_OUTDOOR, s, "OUT NO DATA     ");
}

static void test_outdoor_no_location(void) {
    // Location unset wins over "no data": the user's fix is configuration.
    UiSnapshot s = make_snapshot();
    s.has_location = false;
    s.weather_ok = false;
    assert_render(PAGE_OUTDOOR, s, "SET LOCATION    ");
}

// CUSTOM golden strings: text <=16 renders centered (left-biased); longer
// text marquees — the 16-char window slides over the text plus a 3-space
// wrap gap at 1 char / 300 ms, phase taken from now_us (stateless).
static void test_custom_availability_follows_msg(void) {
    UiSnapshot s = make_snapshot();  // msg is zeroed by default
    TEST_ASSERT_FALSE(page(PAGE_CUSTOM)->available(s));
    strcpy(s.msg, "HI");
    TEST_ASSERT_TRUE(page(PAGE_CUSTOM)->available(s));
    // No other page keys off msg.
    s.msg[0] = '\0';
    TEST_ASSERT_TRUE(page(PAGE_TIME)->available(s));
    TEST_ASSERT_TRUE(page(PAGE_DATE)->available(s));
    TEST_ASSERT_TRUE(page(PAGE_INDOOR)->available(s));
    TEST_ASSERT_TRUE(page(PAGE_OUTDOOR)->available(s));
    TEST_ASSERT_TRUE(page(PAGE_PRESSURE)->available(s));
}

static void test_custom_short_text_centered(void) {
    UiSnapshot s = make_snapshot();
    strcpy(s.msg, "HELLO");  // odd remainder: extra space goes right
    assert_render(PAGE_CUSTOM, s, "     HELLO      ");
    strcpy(s.msg, "HI");
    assert_render(PAGE_CUSTOM, s, "       HI       ");
}

static void test_custom_exactly_16_never_scrolls(void) {
    UiSnapshot s = make_snapshot();
    strcpy(s.msg, "ABCDEFGHIJKLMNOP");
    assert_render_at(PAGE_CUSTOM, s, 0, "ABCDEFGHIJKLMNOP");
    assert_render_at(PAGE_CUSTOM, s, 1500000, "ABCDEFGHIJKLMNOP");
}

static void test_custom_marquee_steps_and_wraps(void) {
    UiSnapshot s = make_snapshot();
    strcpy(s.msg, "THE QUICK BROWN FOX");  // len 19, period 19+3 = 22
    assert_render_at(PAGE_CUSTOM, s, 0, "THE QUICK BROWN ");
    assert_render_at(PAGE_CUSTOM, s, 299999, "THE QUICK BROWN ");   // still frame 0
    assert_render_at(PAGE_CUSTOM, s, 300000, "HE QUICK BROWN F");   // 1 char / 300 ms
    assert_render_at(PAGE_CUSTOM, s, 1200000, "QUICK BROWN FOX ");  // gap enters
    assert_render_at(PAGE_CUSTOM, s, 2100000, "CK BROWN FOX   T");  // wrap through gap
    assert_render_at(PAGE_CUSTOM, s, 6600000, "THE QUICK BROWN ");  // full period
}

static void test_custom_marquee_len_17(void) {
    // Shortest text that scrolls: only 12 text chars left at offset 5.
    UiSnapshot s = make_snapshot();
    strcpy(s.msg, "ABCDEFGHIJKLMNOPQ");  // len 17, period 20
    assert_render_at(PAGE_CUSTOM, s, 0, "ABCDEFGHIJKLMNOP");
    assert_render_at(PAGE_CUSTOM, s, 300000, "BCDEFGHIJKLMNOPQ");
    assert_render_at(PAGE_CUSTOM, s, 600000, "CDEFGHIJKLMNOPQ ");
    assert_render_at(PAGE_CUSTOM, s, 1500000, "FGHIJKLMNOPQ   A");
}

static void test_custom_marquee_len_64(void) {
    // Max length guards the strlen-based modulo math.
    UiSnapshot s = make_snapshot();
    for (int i = 0; i < 64; i++) s.msg[i] = (char)('0' + i % 10);
    s.msg[64] = '\0';  // period 67
    assert_render_at(PAGE_CUSTOM, s, 18000000, "0123   012345678");  // off 60
}

static void test_pressure(void) {
    UiSnapshot s = make_snapshot();
    assert_render(PAGE_PRESSURE, s, "PRES 1013.2 hPa ");
}

static void test_pressure_nan(void) {
    UiSnapshot s = make_snapshot();
    s.hPa = NAN;
    assert_render(PAGE_PRESSURE, s, "PRES SENSOR ERR ");
}

static void test_pressure_availability_follows_probe(void) {
    UiSnapshot s = make_snapshot();
    TEST_ASSERT_TRUE(page(PAGE_PRESSURE)->available(s));
    s.has_pressure = false;
    TEST_ASSERT_FALSE(page(PAGE_PRESSURE)->available(s));
    // Every other page ignores it. OUTDOOR stays in rotation even without
    // a location — SET LOCATION is the configuration hint.
    TEST_ASSERT_TRUE(page(PAGE_TIME)->available(s));
    TEST_ASSERT_TRUE(page(PAGE_DATE)->available(s));
    TEST_ASSERT_TRUE(page(PAGE_INDOOR)->available(s));
    s.has_location = false;
    TEST_ASSERT_TRUE(page(PAGE_OUTDOOR)->available(s));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_registry_has_six_pages);
    RUN_TEST(test_time_24h);
    RUN_TEST(test_time_12h_afternoon);
    RUN_TEST(test_time_12h_midnight);
    RUN_TEST(test_time_12h_noon);
    RUN_TEST(test_time_not_synced);
    RUN_TEST(test_date);
    RUN_TEST(test_date_not_synced);
    RUN_TEST(test_indoor);
    RUN_TEST(test_indoor_sensor_err);
    RUN_TEST(test_outdoor);
    RUN_TEST(test_outdoor_negative_temp_rounds);
    RUN_TEST(test_outdoor_widest_values_fill_line);
    RUN_TEST(test_outdoor_stale_after_45_min);
    RUN_TEST(test_outdoor_no_data);
    RUN_TEST(test_outdoor_no_location);
    RUN_TEST(test_custom_availability_follows_msg);
    RUN_TEST(test_custom_short_text_centered);
    RUN_TEST(test_custom_exactly_16_never_scrolls);
    RUN_TEST(test_custom_marquee_steps_and_wraps);
    RUN_TEST(test_custom_marquee_len_17);
    RUN_TEST(test_custom_marquee_len_64);
    RUN_TEST(test_pressure);
    RUN_TEST(test_pressure_nan);
    RUN_TEST(test_pressure_availability_follows_probe);
    return UNITY_END();
}
