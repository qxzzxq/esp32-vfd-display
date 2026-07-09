// Concrete display pages. Rendering logic is ported verbatim from the
// pre-refactor src/ui.cpp render_* functions — golden strings in
// test/native/test_ui_pages pin the output.

#include "pages.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

class TimePage : public UiPage {
  public:
    void render(char line[17], const UiSnapshot& s, int64_t) const override {
        if (!s.time_valid) {
            snprintf(line, 17, "    --:--:--    ");
            return;
        }
        if (s.use24h) {
            snprintf(line, 17, "    %02d:%02d:%02d    ", s.tm_now.tm_hour,
                     s.tm_now.tm_min, s.tm_now.tm_sec);
        } else {
            int h12 = s.tm_now.tm_hour % 12;
            if (h12 == 0) h12 = 12;
            snprintf(line, 17, "  %2d:%02d:%02d %cM   ", h12, s.tm_now.tm_min,
                     s.tm_now.tm_sec, s.tm_now.tm_hour < 12 ? 'A' : 'P');
        }
    }
    bool rolls_on_change() const override { return true; }
};

class DatePage : public UiPage {
  public:
    void render(char line[17], const UiSnapshot& s, int64_t) const override {
        if (!s.time_valid) {
            snprintf(line, 17, " NO TIME SYNC   ");
            return;
        }
        static const char* DOW[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
        snprintf(line, 17, " %s %04d-%02d-%02d ", DOW[s.tm_now.tm_wday],
                 s.tm_now.tm_year + 1900, s.tm_now.tm_mon + 1, s.tm_now.tm_mday);
    }
};

class IndoorPage : public UiPage {
  public:
    void render(char line[17], const UiSnapshot& s, int64_t) const override {
        if (s.sensors_ok)
            snprintf(line, 17, "IN %5.1fC %4.0f%% ", s.tC, s.rh);
        else
            snprintf(line, 17, "IN  SENSOR ERR  ");
    }
};

class OutdoorPage : public UiPage {
  public:
    void render(char line[17], const UiSnapshot& s, int64_t) const override {
        if (!s.has_location) {
            snprintf(line, 17, "SET LOCATION    ");
            return;
        }
        if (!s.weather_ok) {
            snprintf(line, 17, "OUT NO DATA     ");
            return;
        }
        // %3.0f keeps the widest realistic line ("OUT-10C100% UV11") at 16;
        // typical values render as "OUT 31C 60% UV9".
        int n = snprintf(line, 17, "OUT%3.0fC%3.0f%% UV%.0f", s.out_tC, s.out_rh,
                         s.out_uv);
        if (s.weather_age_s > STALE_S) {
            if (n > 15) n = 15;  // full line: sacrifice the last char
            line[n++] = '?';
        }
        for (; n < 16; n++) line[n] = ' ';
        line[16] = '\0';
    }

  private:
    static constexpr uint32_t STALE_S = 45 * 60;  // > 3 missed 15-min fetches
};

class CustomPage : public UiPage {
  public:
    void render(char line[17], const UiSnapshot& s, int64_t now_us) const override {
        int len = (int)strlen(s.msg);
        if (len <= 16) {  // centered, left-biased (extra space goes right)
            memset(line, ' ', 16);
            memcpy(line + (16 - len) / 2, s.msg, len);
            line[16] = '\0';
            return;
        }
        // Marquee: the 16-char window slides over the text plus a 3-space
        // wrap gap, one char per 300 ms, phase from the monotonic clock
        // (stateless — no scroll position survives page switches).
        const int period = len + 3;
        int off = (int)((now_us / MARQUEE_STEP_US) % period);
        for (int i = 0; i < 16; i++) {
            int idx = (off + i) % period;
            line[i] = idx < len ? s.msg[idx] : ' ';
        }
        line[16] = '\0';
    }
    bool available(const UiSnapshot& s) const override { return s.msg[0] != '\0'; }

  private:
    static constexpr int64_t MARQUEE_STEP_US = 300000;  // 1 char per 300 ms
};

class PressurePage : public UiPage {
  public:
    void render(char line[17], const UiSnapshot& s, int64_t) const override {
        if (!isnan(s.hPa))
            snprintf(line, 17, "PRES %6.1f hPa ", s.hPa);
        else
            snprintf(line, 17, "PRES SENSOR ERR ");
    }
    bool available(const UiSnapshot& s) const override { return s.has_pressure; }
};

TimePage s_time;
DatePage s_date;
IndoorPage s_indoor;
OutdoorPage s_outdoor;
CustomPage s_custom;
PressurePage s_pressure;

UiPage* const s_pages[] = {&s_time, &s_date, &s_indoor, &s_outdoor, &s_custom, &s_pressure};

}  // namespace

UiPage* const* ui_pages(uint8_t* count) {
    *count = sizeof(s_pages) / sizeof(s_pages[0]);
    return s_pages;
}
