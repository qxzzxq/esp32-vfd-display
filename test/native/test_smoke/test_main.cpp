// Smoke test proving the native Unity test env works end to end.
// Replaced by the real UI core suites as they land.
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static void test_native_env_runs(void) {
    TEST_ASSERT_TRUE(true);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_native_env_runs);
    return UNITY_END();
}
