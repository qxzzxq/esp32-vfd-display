#ifndef ui_core_h
#define ui_core_h

// Shared types for the pure UI core.
//
// Everything under src/ui/ is host-testable: no ESP-IDF, FreeRTOS, or project
// headers — libc only. The shell (src/ui.cpp) feeds UiFsm::tick() one input
// per iteration together with a UiSnapshot it built from the producer modules
// (settings/sensors/net), and afterwards draws the returned line and executes
// the returned effects. Nothing in src/ui/ may include anything outside
// src/ui/ or libc.

#include <stdint.h>
#include <time.h>

// One input per tick. None = the idle render tick (~100 ms); the rest map
// 1:1 to encoder.h's EncEvent. Button inputs are raw edges — press-duration
// semantics (click vs long-press) live in UiFsm.
enum class UiInput : uint8_t { None, StepCW, StepCCW, BtnDown, BtnUp };

// Core-local mirror of net.h's NetState (keeps src/ui/ free of project
// headers). The shell converts 1:1.
enum class UiNetState : uint8_t { Portal, Connecting, Connected };

// Read-only view of device state, built by the shell once per tick. Pages and
// menu items render from it with zero calls out, which is what makes them
// deterministic under test.
struct UiSnapshot {
    bool time_valid;    // system clock synced (shell: epoch past MIN_VALID_EPOCH)
    struct tm tm_now;   // local time; zeroed when !time_valid (determinism)
    bool sensors_ok;    // last indoor sensor read succeeded
    float tC;           // indoor temperature, deg C
    float rh;           // indoor relative humidity, %
    float hPa;          // pressure; NAN when BMP280 absent or read failed
    bool has_pressure;  // BMP280 probed OK at boot (gates the PRESSURE page)
    UiNetState net;
    char ap_ssid[16];   // provisioning AP name (portal banner)
    char ip[16];        // STA IP; reserved for the M7 STATUS item
    uint8_t bright;     // saved brightness, driver units 0..240
    bool use24h;
    uint8_t tz_idx;     // M7 TZ item
    uint8_t cycle_s;    // M7 auto-cycle
    char msg[65];       // M6 CUSTOM page text
};

// Side effect requested by the core, executed by the shell after the draw.
struct UiEffect {
    enum class Type : uint8_t {
        SetBrightness,     // arg = driver units; display-only (live preview / restore)
        CommitBrightness,  // arg = driver units; persist via settings_save
        WifiReset,         // erase credentials and reboot into the portal
    };
    Type type;
    uint8_t arg;
};

// Result of one UiFsm::tick(): the full 16-char line to draw plus any effects.
struct UiOutput {
    char line[17];
    // A long-press fired this tick and the line shows the completed hold bar:
    // the shell pauses ~200 ms after drawing so the full bar registers before
    // the new mode renders. Pacing only — no device state involved.
    bool hold_fired;
    uint8_t effect_count;
    UiEffect effects[4];  // fixed capacity; the worst real tick emits one

    void emit(UiEffect::Type t, uint8_t arg = 0) {
        if (effect_count < sizeof(effects) / sizeof(effects[0]))
            effects[effect_count++] = UiEffect{t, arg};
    }
};

// Gesture / timeout thresholds (see UiFsm).
constexpr int64_t UI_MENU_TIMEOUT_US = 20 * 1000000LL;
constexpr int64_t UI_LONG_PRESS_US = 1500 * 1000LL;
// Show hold progress after this; clicks stay clean. The 900 ms fill window is
// exactly 9 segments x the 100 ms tick, so the bar advances once per render —
// any other ratio beats against the tick and stalls some segments for 2 ticks.
constexpr int64_t UI_HOLD_SHOW_US = 600 * 1000LL;

#endif
