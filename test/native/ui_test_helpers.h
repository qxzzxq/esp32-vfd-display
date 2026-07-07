#ifndef ui_test_helpers_h
#define ui_test_helpers_h

// Shared fixtures for the native UI core suites. Lives outside the test_*
// dirs so the PlatformIO runner doesn't treat it as a suite.

#include <string.h>

#include "ui/core.h"
#include "ui/fsm.h"
#include "ui/menu_items.h"
#include "ui/pages.h"

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

// Zeroed UiOutput (UiFsm::tick resets it itself; direct page/item tests need
// a clean one).
inline UiOutput make_output() {
    UiOutput o;
    memset(&o, 0, sizeof(o));
    return o;
}

// UiFsm wired to the real page/menu registries.
inline UiFsm make_fsm() {
    uint8_t pc = 0, ic = 0;
    UiPage* const* pages = ui_pages(&pc);
    MenuItem* const* items = ui_menu_items(&ic);
    return UiFsm(pages, pc, items, ic);
}

// Drives a UiFsm with a fake monotonic clock advancing in the firmware's
// 100 ms render-tick steps, mirroring the shell's loop.
struct FsmDriver {
    UiSnapshot snap = make_snapshot();
    int64_t now_us = 1000000;
    UiOutput out = make_output();
    UiFsm fsm = make_fsm();

    // Send one input at the current fake time (no time advance).
    const UiOutput& feed(UiInput in) {
        fsm.tick(in, now_us, snap, &out);
        return out;
    }

    // Advance the clock through n idle 100 ms render ticks.
    const UiOutput& idle(int n = 1) {
        for (int i = 0; i < n; i++) {
            now_us += 100000;
            fsm.tick(UiInput::None, now_us, snap, &out);
        }
        return out;
    }

    // Short click: press, then release one tick (100 ms) later.
    const UiOutput& click() {
        feed(UiInput::BtnDown);
        now_us += 100000;
        return feed(UiInput::BtnUp);
    }

    // Hold the button through idle ticks until the long-press fires at the
    // 1.0 s threshold. The release is deliberately not sent (it is swallowed).
    const UiOutput& long_press() {
        feed(UiInput::BtnDown);
        return idle(10);
    }
};

#endif
