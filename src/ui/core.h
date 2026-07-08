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
    bool has_location;      // lat/lon configured; false ⇒ OUTDOOR shows SET LOCATION
    bool weather_ok;        // at least one successful Open-Meteo fetch
    float out_tC;           // outdoor temperature, deg C
    float out_rh;           // outdoor relative humidity, %
    float out_uv;           // UV index
    uint32_t weather_age_s; // seconds since the last successful fetch (staleness)
    UiNetState net;
    char ap_ssid[16];   // provisioning AP name (portal banner)
    char ip[16];        // STA IP; reserved for the M7 STATUS item
    uint8_t bright;     // saved brightness, driver units 0..240
    bool use24h;
    uint8_t tz_idx;     // M7 TZ item
    uint8_t cycle_s;    // M7 auto-cycle
    char msg[65];       // CUSTOM page text (POST /api/message)
    uint32_t msg_seq;   // bumps on every accepted POST; a change with a
                        // non-empty msg jumps the display to CUSTOM
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
    // An animation (roll or visible hold bar) is in flight: the shell
    // shortens its render tick from 100 ms to 40 ms while set.
    bool animating;
    // Desired CGRAM contents for slots 1..7, fully specified every tick
    // ([0] is unused — code 0x00 can't appear in the NUL-terminated line).
    // Idle ticks carry the static bar/arrow glyphs; an active roll overwrites
    // slots with per-frame composites. The shell diffs against its cache and
    // uploads only changes, which also restores the static glyphs after a
    // roll borrowed their slots.
    uint8_t glyphs[8][5];
    uint8_t effect_count;
    UiEffect effects[4];  // fixed capacity; the worst real tick emits one

    void emit(UiEffect::Type t, uint8_t arg = 0) {
        if (effect_count < sizeof(effects) / sizeof(effects[0]))
            effects[effect_count++] = UiEffect{t, arg};
    }
};

// Gesture / timeout thresholds (see UiFsm).
constexpr int64_t UI_MENU_TIMEOUT_US = 20 * 1000000LL;
constexpr int64_t UI_LONG_PRESS_US = 1000 * 1000LL;
// Show hold progress after this; clicks stay clean. The fill is column-
// granular (UI_HOLD_BAR_SEGS cells x 5 columns over the 500 ms window): at
// the idle 100 ms tick the bar advances one full cell per render; finer
// render ticks reveal the partial-cell CGRAM glyphs in between.
constexpr int64_t UI_HOLD_SHOW_US = 500 * 1000LL;
constexpr int UI_HOLD_BAR_SEGS = 5;

// Vertical roll animation (split-flap), one row of travel per 40 ms step,
// all cells in lockstep. Content rolls (TIME ticks) use a 1-row gap: 8 steps
// = 320 ms, old and new glyph briefly share the cell, so CGRAM slots are
// keyed by from->to pair. Page rolls use a 7-row gap: 14 steps = 560 ms, the
// whole old line rolls fully out before the new line rolls in — each frame
// then shows a single char per cell, so slots are keyed per character and 7
// slots cover the whole 16-cell line (see UiFsm::apply_roll for the rare
// overflow fallback).
constexpr int UI_ROLL_STEPS = 8;        // content roll: 7 rows + 1 gap row
constexpr int UI_PAGE_ROLL_STEPS = 14;  // page roll: 7 rows + 7 gap rows
constexpr int64_t UI_ROLL_STEP_US = 40 * 1000LL;

#endif
