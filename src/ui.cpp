#include "ui.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "VFDDisplay.h"
#include "driver/gpio.h"
#include "encoder.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "sensors.h"
#include "settings.h"
#include "ui/fsm.h"
#include "ui/menu_items.h"
#include "ui/pages.h"
#include "weather.h"
#include "web.h"

// Platform shell over the pure UI core (src/ui/): owns the display and the
// encoder loop, builds the per-tick UiSnapshot from the producer modules,
// and executes the UiEffects the core returns. All UI logic and rendering
// live in the core, covered by test/native/.

// VFD pins (this module is the sole owner of the display)
#define VFD_CS  GPIO_NUM_6
#define VFD_CLK GPIO_NUM_7
#define VFD_DIN GPIO_NUM_21

#define UI_TICK_MS 100      // render tick while idle
#define UI_TICK_ANIM_MS 30  // render tick while animating; = UI_ROLL_STEP_US (one step/frame)

// System time is bogus (starts at 1970) until SNTP sets it (M4).
#define MIN_VALID_EPOCH 1609459200  // 2021-01-01

static VFDDisplay s_vfd(VFD_CS, VFD_CLK, VFD_DIN, 16);

// Gather the data view the core renders from, one copy-out getter each.
static UiSnapshot build_snapshot() {
    UiSnapshot s;
    memset(&s, 0, sizeof(s));

    time_t now = time(nullptr);
    s.time_valid = now >= MIN_VALID_EPOCH;
    if (s.time_valid) localtime_r(&now, &s.tm_now);  // stays zeroed otherwise

    s.sensors_ok = sensors_get(&s.tC, &s.rh, &s.hPa);
    s.has_pressure = sensors_has_pressure();

    Weather w;
    s.weather_ok = weather_get(&w);
    if (s.weather_ok) {
        s.out_tC = w.tC;
        s.out_rh = w.rh;
        s.out_uv = w.uv;
        s.weather_age_s = (uint32_t)((esp_timer_get_time() - w.fetched_us) / 1000000);
    }

    switch (net_state()) {
        case NetState::Portal: s.net = UiNetState::Portal; break;
        case NetState::Connecting: s.net = UiNetState::Connecting; break;
        case NetState::Connected: s.net = UiNetState::Connected; break;
    }
    net_get_ap_ssid(s.ap_ssid);
    net_get_ip(s.ip);
    strlcpy(s.version, esp_app_get_description()->version, sizeof(s.version));

    Settings st = settings_get();
    s.has_location = st.lat[0] != '\0' && st.lon[0] != '\0';
    s.bright = st.bright;
    s.use24h = st.use24h != 0;
    s.tz_idx = st.tz_idx;
    s.tz_count = (uint8_t)tz_count();
    s.tz_names = tz_names();
    s.cycle_s = st.cycle_s;
    s.msg_seq = web_get_message(s.msg);
    return s;
}

static void execute_effect(const UiEffect& e) {
    switch (e.type) {
        case UiEffect::Type::SetBrightness:
            s_vfd.setBrightness(e.arg);  // display only: live preview / restore
            break;
        case UiEffect::Type::CommitBrightness: {
            Settings st = settings_get();
            st.bright = e.arg;
            settings_save(st);
            break;
        }
        case UiEffect::Type::CommitUse24h: {
            Settings st = settings_get();
            st.use24h = e.arg;
            settings_save(st);
            break;
        }
        case UiEffect::Type::CommitTz: {
            Settings st = settings_get();
            st.tz_idx = e.arg;
            settings_save(st);  // settings_save re-applies the timezone
            break;
        }
        case UiEffect::Type::CommitCycle: {
            Settings st = settings_get();
            st.cycle_s = e.arg;
            settings_save(st);
            break;
        }
        case UiEffect::Type::WifiReset:
            net_reset_credentials();  // reboots into the portal
            break;
    }
}

void ui_run() {
    // Single-core C3: keep this UI task above the net worker (prio 3, net.cpp)
    // so render bursts preempt its CPU-heavy work (TLS on the first weather
    // fetch) instead of stalling animation frames — otherwise a roll drops its
    // tail frames and finishes with a stutter during the post-boot network
    // burst. The UI blocks on the encoder queue between frames, so the worker
    // still gets essentially all the CPU.
    vTaskPrioritySet(nullptr, 4);

    s_vfd.init();
    s_vfd.clear();
    s_vfd.setBrightness(settings_get().bright);

    uint8_t page_count = 0, item_count = 0;
    UiPage* const* pages = ui_pages(&page_count);
    MenuItem* const* items = ui_menu_items(&item_count);
    UiFsm fsm(pages, page_count, items, item_count);
    UiOutput out = {};

    // Per-slot cache of what CGRAM currently holds. Invalid at boot
    // (controller reset cleared CGRAM), so the first tick uploads the core's
    // desired glyphs; afterwards only diffs go over SPI — which also restores
    // the bar/arrow glyphs after a roll animation borrowed their slots.
    uint8_t cgram[8][5];
    uint8_t cgram_valid = 0;  // bitmask by slot

    while (1) {
        // While idle, wake at the next whole second instead of on a blind
        // UI_TICK_MS grid, so a seconds change on the TIME page is noticed on
        // the beat and its roll starts within one FreeRTOS tick of the second
        // (not up to UI_TICK_MS late, which jitters second to second). Capped
        // at UI_TICK_MS so sensors/net/menu-timeout still get serviced, and
        // floored at one tick so we never busy-spin just before the boundary.
        uint32_t wait_ms;
        if (out.animating) {
            wait_ms = UI_TICK_ANIM_MS;
        } else {
            struct timeval tv;
            gettimeofday(&tv, nullptr);
            uint32_t to_next_ms = (uint32_t)((1000000 - tv.tv_usec) / 1000);
            wait_ms = to_next_ms < UI_TICK_MS ? to_next_ms : UI_TICK_MS;
            if (wait_ms < portTICK_PERIOD_MS) wait_ms = portTICK_PERIOD_MS;
        }

        EncEvent ev;
        bool got = encoder_wait(&ev, pdMS_TO_TICKS(wait_ms));
        int64_t now_us = esp_timer_get_time();

        UiInput in = UiInput::None;
        if (got) {
            switch (ev) {
                case EncEvent::StepCW: in = UiInput::StepCW; break;
                case EncEvent::StepCCW: in = UiInput::StepCCW; break;
                case EncEvent::BtnDown: in = UiInput::BtnDown; break;
                case EncEvent::BtnUp: in = UiInput::BtnUp; break;
            }
        }

        UiSnapshot snap = build_snapshot();
        fsm.tick(in, now_us, snap, &out);

        // CGRAM before DCRAM so a line never references a stale glyph.
        for (int slot = 1; slot <= 7; slot++) {
            if ((cgram_valid & (1u << slot)) &&
                memcmp(cgram[slot], out.glyphs[slot], 5) == 0)
                continue;
            s_vfd.setCustomChar(slot, out.glyphs[slot]);
            memcpy(cgram[slot], out.glyphs[slot], 5);
            cgram_valid |= 1u << slot;
        }
        s_vfd.writeString(0, out.line);

        // A long-press just fired and the completed hold bar was drawn; let it
        // register before acting. Encoder events queued during the pause are
        // handled afterwards, in the new mode (same pacing as pre-refactor).
        if (out.hold_fired) vTaskDelay(pdMS_TO_TICKS(200));

        // Effects run after the draw; the display-brightness ones land a few
        // ms behind the line they accompany, which is not visible.
        for (int i = 0; i < out.effect_count; i++) execute_effect(out.effects[i]);
    }
}
