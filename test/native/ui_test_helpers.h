#ifndef ui_test_helpers_h
#define ui_test_helpers_h

// Shared fixtures for the native UI core suites. Lives outside the test_*
// dirs so the PlatformIO runner doesn't treat it as a suite.

#include <string.h>

#include "ui/core.h"

// Snapshot with sane defaults: synced clock at SAT 2026-07-05 14:25:36,
// sensors OK (23.4 C / 47 % / 1013.2 hPa, BMP280 present), WiFi connected,
// brightness 128 (menu units 8), 24-hour mode. Tests tweak fields from here.
inline UiSnapshot make_snapshot() {
    UiSnapshot s;
    memset(&s, 0, sizeof(s));
    s.time_valid = true;
    s.tm_now.tm_hour = 14;
    s.tm_now.tm_min = 25;
    s.tm_now.tm_sec = 36;
    s.tm_now.tm_wday = 6;  // SAT
    s.tm_now.tm_year = 2026 - 1900;
    s.tm_now.tm_mon = 7 - 1;
    s.tm_now.tm_mday = 5;
    s.sensors_ok = true;
    s.tC = 23.4f;
    s.rh = 47.0f;
    s.hPa = 1013.2f;
    s.has_pressure = true;
    s.net = UiNetState::Connected;
    strcpy(s.ap_ssid, "VFD-ABCD");
    s.bright = 128;
    s.use24h = true;
    return s;
}

#endif
