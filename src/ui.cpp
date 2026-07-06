#include "ui.h"

#include <math.h>
#include <stdio.h>
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

// VFD pins (this module is the sole owner of the display)
#define VFD_CS  GPIO_NUM_6
#define VFD_CLK GPIO_NUM_7
#define VFD_DIN GPIO_NUM_21

#define UI_TICK_MS      100    // render tick while idle
#define MENU_TIMEOUT_US (20 * 1000000LL)
#define LONG_PRESS_US   (1500 * 1000LL)
// Show hold progress after this; clicks stay clean. The 900 ms fill window is
// exactly 9 segments x the 100 ms tick, so the bar advances once per render —
// any other ratio beats against the tick and stalls some segments for 2 ticks.
#define HOLD_SHOW_US    (600 * 1000LL)

// System time is bogus (starts at 1970) until SNTP sets it (M4).
#define MIN_VALID_EPOCH 1609459200  // 2021-01-01

static VFDDisplay s_vfd(VFD_CS, VFD_CLK, VFD_DIN, 16);

enum class Page : uint8_t { Time, Date, Indoor, Pressure, Count };
enum class Mode : uint8_t { Pages, Menu, Edit };
// Menu grows with milestones; 24H, TZ, CYCLE and STATUS arrive with M7.
enum MenuItem : int { MI_BRIGHT, MI_WIFIRST, MI_EXIT, MI_COUNT };

static Page s_page = Page::Time;
static Mode s_mode = Mode::Pages;
static int s_menu_item = MI_BRIGHT;
static int s_edit_bright;       // menu units 1..15 (driver = x16)
static int64_t s_last_input_us;
static int64_t s_btn_down_us = -1;

static void render_time(char* line, const Settings& st) {
    time_t now = time(nullptr);
    if (now < MIN_VALID_EPOCH) {
        snprintf(line, 17, "    --:--:--    ");
        return;
    }
    struct tm t;
    localtime_r(&now, &t);
    if (st.use24h) {
        snprintf(line, 17, "    %02d:%02d:%02d    ", t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        int h12 = t.tm_hour % 12;
        if (h12 == 0) h12 = 12;
        snprintf(line, 17, "  %2d:%02d:%02d %cM   ", h12, t.tm_min, t.tm_sec,
                 t.tm_hour < 12 ? 'A' : 'P');
    }
}

static void render_date(char* line) {
    time_t now = time(nullptr);
    if (now < MIN_VALID_EPOCH) {
        snprintf(line, 17, " NO TIME SYNC   ");
        return;
    }
    struct tm t;
    localtime_r(&now, &t);
    static const char* DOW[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    snprintf(line, 17, " %s %04d-%02d-%02d ", DOW[t.tm_wday], t.tm_year + 1900,
             t.tm_mon + 1, t.tm_mday);
}

static void render_indoor(char* line) {
    float tC, rh, hPa;
    if (sensors_get(&tC, &rh, &hPa))
        snprintf(line, 17, "IN %5.1fC %4.0f%% ", tC, rh);
    else
        snprintf(line, 17, "IN  SENSOR ERR  ");
}

static void render_pressure(char* line) {
    float tC, rh, hPa;
    sensors_get(&tC, &rh, &hPa);
    if (!isnan(hPa))
        snprintf(line, 17, "PRES %6.1f hPa ", hPa);
    else
        snprintf(line, 17, "PRES SENSOR ERR ");
}

static void render(int64_t now_us) {
    char line[17];
    Settings st = settings_get();

    if (s_btn_down_us >= 0 && now_us - s_btn_down_us >= HOLD_SHOW_US) {
        // Hold progress bar (pages: enter menu; menu: exit), full at the threshold
        int filled = (int)((now_us - s_btn_down_us - HOLD_SHOW_US) * 9 /
                           (LONG_PRESS_US - HOLD_SHOW_US));
        if (filled > 9) filled = 9;
        char bar[10];
        for (int i = 0; i < 9; i++) bar[i] = i < filled ? '=' : ' ';
        bar[9] = '\0';
        snprintf(line, 17, "%s [%s]", s_mode == Mode::Pages ? "MENU" : "EXIT", bar);
    } else if (s_mode == Mode::Pages && net_state() == NetState::Portal) {
        // Provisioning: alternate the AP name and the portal address every 3 s
        if ((now_us / 3000000LL) % 2 == 0) {
            char ap[16];
            net_get_ap_ssid(ap);
            snprintf(line, 17, "SETUP  %-9s", ap);
        } else {
            snprintf(line, 17, "AP 192.168.4.1  ");
        }
    } else if (s_mode == Mode::Pages) {
        switch (s_page) {
            case Page::Time: render_time(line, st); break;
            case Page::Date: render_date(line); break;
            case Page::Indoor: render_indoor(line); break;
            case Page::Pressure: render_pressure(line); break;
            default: line[0] = '\0'; break;
        }
    } else {
        switch (s_menu_item) {
            case MI_BRIGHT:
                // Edit mode is indicated by the ">" cursor moving to the value side
                if (s_mode == Mode::Edit) {
                    char v[8];
                    snprintf(v, sizeof(v), ">%d", s_edit_bright);
                    snprintf(line, 17, " BRIGHT %8s", v);
                } else {
                    snprintf(line, 17, ">BRIGHT %8d", st.bright / 16);
                }
                break;
            case MI_WIFIRST:
                if (s_mode == Mode::Edit)
                    snprintf(line, 17, "CLICK = CONFIRM ");
                else
                    snprintf(line, 17, ">WIFI RESET     ");
                break;
            case MI_EXIT:
            default:
                snprintf(line, 17, ">EXIT           ");
                break;
        }
    }
    s_vfd.writeString(0, line);
}

// Short-press action, dispatched on button release.
static void handle_click() {
    Settings st;
    switch (s_mode) {
        case Mode::Pages:
            // menu entry is long-press (MENU bar); click on the pages is unassigned
            break;
        case Mode::Menu:
            if (s_menu_item == MI_BRIGHT) {
                s_edit_bright = settings_get().bright / 16;
                if (s_edit_bright < 1) s_edit_bright = 1;
                if (s_edit_bright > 15) s_edit_bright = 15;
                s_mode = Mode::Edit;
            } else if (s_menu_item == MI_WIFIRST) {
                s_mode = Mode::Edit;  // "CLICK = CONFIRM" armed
            } else {  // MI_EXIT
                s_mode = Mode::Pages;
            }
            break;
        case Mode::Edit:
            if (s_menu_item == MI_WIFIRST) {
                net_reset_credentials();  // reboots into the portal
            } else {  // MI_BRIGHT
                st = settings_get();
                st.bright = s_edit_bright * 16;
                settings_save(st);
                s_mode = Mode::Menu;
            }
            break;
    }
}

static void handle_step(bool cw) {
    int dir = cw ? 1 : -1;
    switch (s_mode) {
        case Mode::Pages:
            do {
                s_page = (Page)(((int)s_page + dir + (int)Page::Count) % (int)Page::Count);
            } while (s_page == Page::Pressure && !sensors_has_pressure());
            break;
        case Mode::Menu:
            s_menu_item = (s_menu_item + dir + MI_COUNT) % MI_COUNT;
            break;
        case Mode::Edit:
            if (s_menu_item == MI_WIFIRST) {
                s_mode = Mode::Menu;  // rotate = cancel the confirm
                break;
            }
            s_edit_bright += dir;
            if (s_edit_bright < 1) s_edit_bright = 1;   // keep the display visible
            if (s_edit_bright > 15) s_edit_bright = 15;
            s_vfd.setBrightness(s_edit_bright * 16);    // live preview
            break;
    }
}

void ui_run() {
    s_vfd.init();
    s_vfd.clear();
    s_vfd.setBrightness(settings_get().bright);

    s_last_input_us = esp_timer_get_time();
    while (1) {
        EncEvent ev;
        bool got = encoder_wait(&ev, pdMS_TO_TICKS(UI_TICK_MS));
        int64_t now_us = esp_timer_get_time();

        if (got) {
            s_last_input_us = now_us;
            switch (ev) {
                case EncEvent::StepCW: handle_step(true); break;
                case EncEvent::StepCCW: handle_step(false); break;
                case EncEvent::BtnDown:
                    s_btn_down_us = now_us;
                    break;
                case EncEvent::BtnUp:
                    if (s_btn_down_us >= 0 && now_us - s_btn_down_us < LONG_PRESS_US)
                        handle_click();
                    // a full long-press already fired at the threshold below
                    s_btn_down_us = -1;
                    break;
            }
        } else if (s_mode != Mode::Pages &&
                   now_us - s_last_input_us > MENU_TIMEOUT_US) {
            // Inactivity: abandon the menu (and any uncommitted brightness edit)
            s_mode = Mode::Pages;
            s_vfd.setBrightness(settings_get().bright);
        }

        render(now_us);

        // Long-press fires while still held: from the pages it opens the menu,
        // from the menu it escapes. render() has just drawn the completed bar
        // (filled clamps at 9); let it register, then act. The eventual release
        // is swallowed via s_btn_down_us = -1.
        if (s_btn_down_us >= 0 && now_us - s_btn_down_us >= LONG_PRESS_US) {
            vTaskDelay(pdMS_TO_TICKS(200));
            if (s_mode == Mode::Pages) {
                s_mode = Mode::Menu;
                s_menu_item = MI_BRIGHT;
            } else {
                s_mode = Mode::Pages;
                s_vfd.setBrightness(settings_get().bright);
            }
            s_btn_down_us = -1;
        }
    }
}
