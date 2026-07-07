// Concrete display pages. Rendering logic is ported verbatim from the
// pre-refactor src/ui.cpp render_* functions — golden strings in
// test/native/test_ui_pages pin the output.

#include "pages.h"

#include <math.h>
#include <stdio.h>

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
PressurePage s_pressure;

UiPage* const s_pages[] = {&s_time, &s_date, &s_indoor, &s_pressure};

}  // namespace

UiPage* const* ui_pages(uint8_t* count) {
    *count = sizeof(s_pages) / sizeof(s_pages[0]);
    return s_pages;
}
