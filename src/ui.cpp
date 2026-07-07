#include "ui.h"

#include <string.h>
#include <time.h>

#include "VFDDisplay.h"
#include "driver/gpio.h"
#include "encoder.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "sensors.h"
#include "settings.h"
#include "ui/fsm.h"
#include "ui/menu_items.h"
#include "ui/pages.h"

// Platform shell over the pure UI core (src/ui/): owns the display and the
// encoder loop, builds the per-tick UiSnapshot from the producer modules,
// and executes the UiEffects the core returns. All UI logic and rendering
// live in the core, covered by test/native/.

// VFD pins (this module is the sole owner of the display)
#define VFD_CS  GPIO_NUM_6
#define VFD_CLK GPIO_NUM_7
#define VFD_DIN GPIO_NUM_21

#define UI_TICK_MS 100  // render tick while idle

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

    switch (net_state()) {
        case NetState::Portal: s.net = UiNetState::Portal; break;
        case NetState::Connecting: s.net = UiNetState::Connecting; break;
        case NetState::Connected: s.net = UiNetState::Connected; break;
    }
    net_get_ap_ssid(s.ap_ssid);
    net_get_ip(s.ip);

    Settings st = settings_get();
    s.bright = st.bright;
    s.use24h = st.use24h != 0;
    s.tz_idx = st.tz_idx;
    s.cycle_s = st.cycle_s;
    memcpy(s.msg, st.msg, sizeof(s.msg));
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
        case UiEffect::Type::WifiReset:
            net_reset_credentials();  // reboots into the portal
            break;
    }
}

void ui_run() {
    s_vfd.init();
    s_vfd.clear();
    s_vfd.setBrightness(settings_get().bright);

    uint8_t page_count = 0, item_count = 0;
    UiPage* const* pages = ui_pages(&page_count);
    MenuItem* const* items = ui_menu_items(&item_count);
    UiFsm fsm(pages, page_count, items, item_count);
    UiOutput out;

    while (1) {
        EncEvent ev;
        bool got = encoder_wait(&ev, pdMS_TO_TICKS(UI_TICK_MS));
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
