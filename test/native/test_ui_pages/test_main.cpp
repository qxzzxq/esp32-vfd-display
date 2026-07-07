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
enum { PAGE_TIME, PAGE_DATE, PAGE_INDOOR, PAGE_PRESSURE, PAGE_COUNT };

static const UiPage* page(int idx) {
    uint8_t n = 0;
    UiPage* const* pages = ui_pages(&n);
    TEST_ASSERT_TRUE(idx < n);
    return pages[idx];
}

static void assert_render(int idx, const UiSnapshot& s, const char* expect) {
    char line[17];
    memset(line, '#', sizeof(line));  // catch missing NUL / short writes
    page(idx)->render(line, s, 0);
    TEST_ASSERT_EQUAL_INT(16, (int)strlen(line));
    TEST_ASSERT_EQUAL_STRING(expect, line);
}

static void test_registry_has_four_pages(void) {
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
    // Every other page ignores it.
    TEST_ASSERT_TRUE(page(PAGE_TIME)->available(s));
    TEST_ASSERT_TRUE(page(PAGE_DATE)->available(s));
    TEST_ASSERT_TRUE(page(PAGE_INDOOR)->available(s));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_registry_has_four_pages);
    RUN_TEST(test_time_24h);
    RUN_TEST(test_time_12h_afternoon);
    RUN_TEST(test_time_12h_midnight);
    RUN_TEST(test_time_12h_noon);
    RUN_TEST(test_time_not_synced);
    RUN_TEST(test_date);
    RUN_TEST(test_date_not_synced);
    RUN_TEST(test_indoor);
    RUN_TEST(test_indoor_sensor_err);
    RUN_TEST(test_pressure);
    RUN_TEST(test_pressure_nan);
    RUN_TEST(test_pressure_availability_follows_probe);
    return UNITY_END();
}
