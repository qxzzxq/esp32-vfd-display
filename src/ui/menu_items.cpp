// Concrete menu items. Rendering and edit behavior are ported verbatim from
// the pre-refactor src/ui.cpp (render()'s menu switch, handle_click,
// handle_step) — test/native/test_ui_menu pins the behavior.

#include "menu_items.h"

#include <stdio.h>

#include "glyphs.h"

namespace {

// Brightness value edit: menu units 1..15, driver = x16. Rotation previews
// live (display only); the commit click persists.
class BrightItem : public MenuItem {
  public:
    void render(char line[17], bool editing, const UiSnapshot& s) const override {
        // Edit mode is indicated by the arrow cursor moving to the value side
        if (editing) {
            char v[8];
            snprintf(v, sizeof(v), "%c%d", UI_GLYPH_ARROW, val_);
            snprintf(line, 17, " BRIGHT %8s", v);
        } else {
            snprintf(line, 17, "%cBRIGHT %8d", UI_GLYPH_ARROW, s.bright / 16);
        }
    }
    ClickResult on_click(const UiSnapshot& s, UiOutput&) override {
        val_ = s.bright / 16;
        if (val_ < 1) val_ = 1;
        if (val_ > 15) val_ = 15;
        return ClickResult::EnterEdit;
    }
    bool edit_step(int dir, const UiSnapshot&, UiOutput& out) override {
        val_ += dir;
        if (val_ < 1) val_ = 1;  // keep the display visible
        if (val_ > 15) val_ = 15;
        out.emit(UiEffect::Type::SetBrightness, (uint8_t)(val_ * 16));  // live preview
        return true;
    }
    bool edit_click(UiSnapshot& s, UiOutput& out) override {
        s.bright = (uint8_t)(val_ * 16);  // view first, so this tick renders it
        out.emit(UiEffect::Type::CommitBrightness, s.bright);
        return true;
    }
    void edit_abort(const UiSnapshot& s, UiOutput& out) override {
        out.emit(UiEffect::Type::SetBrightness, s.bright);  // undo the preview
    }

  private:
    int val_ = 1;  // menu units 1..15; reseeded by on_click on every edit entry
};

// 24-hour clock toggle. Two-valued, so a rotate in either direction flips it;
// the commit persists and the TIME/DATE pages pick the new format up next tick.
class Use24hItem : public MenuItem {
  public:
    void render(char line[17], bool editing, const UiSnapshot& s) const override {
        const char* v = (editing ? val_ : s.use24h != 0) ? "ON" : "OFF";
        if (editing) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%c%s", UI_GLYPH_ARROW, v);
            snprintf(line, 17, " 24H %11s", buf);
        } else {
            snprintf(line, 17, "%c24H %11s", UI_GLYPH_ARROW, v);
        }
    }
    ClickResult on_click(const UiSnapshot& s, UiOutput&) override {
        val_ = s.use24h != 0;
        return ClickResult::EnterEdit;
    }
    bool edit_step(int, const UiSnapshot&, UiOutput&) override {
        val_ = !val_;
        return true;
    }
    bool edit_click(UiSnapshot& s, UiOutput& out) override {
        s.use24h = val_;  // view first, so this tick renders the committed value
        out.emit(UiEffect::Type::CommitUse24h, val_ ? 1 : 0);
        return true;
    }

  private:
    bool val_ = false;
};

// Timezone selection: rotate walks the table, the name is looked up from the
// snapshot (the pure core has no access to settings' table). Commit re-applies
// the TZ in the shell so the clock shifts immediately.
class TzItem : public MenuItem {
  public:
    void render(char line[17], bool editing, const UiSnapshot& s) const override {
        int i = editing ? idx_ : s.tz_idx;
        const char* name = (s.tz_names && i < s.tz_count) ? s.tz_names[i] : "";
        if (editing) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%c%s", UI_GLYPH_ARROW, name);
            snprintf(line, 17, " TZ %12s", buf);
        } else {
            snprintf(line, 17, "%cTZ %12s", UI_GLYPH_ARROW, name);
        }
    }
    ClickResult on_click(const UiSnapshot& s, UiOutput&) override {
        idx_ = s.tz_idx;
        return ClickResult::EnterEdit;
    }
    bool edit_step(int dir, const UiSnapshot& s, UiOutput&) override {
        if (s.tz_count > 0) idx_ = (idx_ + dir + s.tz_count) % s.tz_count;
        return true;
    }
    bool edit_click(UiSnapshot& s, UiOutput& out) override {
        s.tz_idx = (uint8_t)idx_;
        out.emit(UiEffect::Type::CommitTz, (uint8_t)idx_);
        return true;
    }

  private:
    int idx_ = 0;
};

// Auto-cycle interval: OFF / 5 / 10 / 30 / 60 s, cycled as a fixed option list.
class CycleItem : public MenuItem {
  public:
    void render(char line[17], bool editing, const UiSnapshot& s) const override {
        char v[8];
        fmt(v, sizeof(v), editing ? OPTS[idx_] : s.cycle_s);
        if (editing) {
            char buf[10];
            snprintf(buf, sizeof(buf), "%c%s", UI_GLYPH_ARROW, v);
            snprintf(line, 17, " CYCLE %9s", buf);
        } else {
            snprintf(line, 17, "%cCYCLE %9s", UI_GLYPH_ARROW, v);
        }
    }
    ClickResult on_click(const UiSnapshot& s, UiOutput&) override {
        idx_ = opt_index(s.cycle_s);
        return ClickResult::EnterEdit;
    }
    bool edit_step(int dir, const UiSnapshot&, UiOutput&) override {
        idx_ = (idx_ + dir + NOPTS) % NOPTS;
        return true;
    }
    bool edit_click(UiSnapshot& s, UiOutput& out) override {
        s.cycle_s = OPTS[idx_];
        out.emit(UiEffect::Type::CommitCycle, OPTS[idx_]);
        return true;
    }

  private:
    static constexpr int NOPTS = 5;
    static constexpr uint8_t OPTS[NOPTS] = {0, 5, 10, 30, 60};
    static void fmt(char* v, size_t n, uint8_t sec) {
        if (sec == 0)
            snprintf(v, n, "OFF");
        else
            snprintf(v, n, "%ds", sec);
    }
    static int opt_index(uint8_t sec) {
        for (int i = 0; i < NOPTS; i++)
            if (OPTS[i] == sec) return i;
        return 0;  // an unknown saved value shows as OFF
    }
    int idx_ = 0;
};

// Read-only "about" overlay: click opens it, rotation pages between the device
// IP (or WiFi state) and the firmware version, a click dismisses. Not a value
// edit — it borrows the edit sub-mode as a transient two-page overlay. The
// pages crossfade because UiFsm::screen_id folds edit_subscreen() into the id.
class AboutItem : public MenuItem {
  public:
    void render(char line[17], bool editing, const UiSnapshot& s) const override {
        if (!editing) {
            snprintf(line, 17, "%cABOUT          ", UI_GLYPH_ARROW);
        } else if (page_ == 0) {
            switch (s.net) {
                case UiNetState::Connected:
                    snprintf(line, 17, "%-16s", s.ip[0] ? s.ip : "NO IP");
                    break;
                case UiNetState::Connecting:
                    snprintf(line, 17, "WIFI CONNECTING ");
                    break;
                case UiNetState::Portal:
                    snprintf(line, 17, "AP 192.168.4.1  ");
                    break;
            }
        } else {
            snprintf(line, 17, "VER %-12s", s.version);
        }
    }
    ClickResult on_click(const UiSnapshot&, UiOutput&) override {
        page_ = 0;  // always open on the IP page
        return ClickResult::EnterEdit;
    }
    bool edit_step(int dir, const UiSnapshot&, UiOutput&) override {
        page_ = (page_ + dir + NPAGES) % NPAGES;  // rotation pages; never dismisses
        return true;
    }
    bool edit_click(UiSnapshot&, UiOutput&) override {
        return true;  // a click dismisses back to the menu
    }
    uint8_t edit_subscreen() const override { return (uint8_t)page_; }

  private:
    static constexpr int NPAGES = 2;
    int page_ = 0;
};

// Two-step credential erase: click arms the confirm, rotate cancels, a
// second click fires (the shell reboots into the portal).
class WifiResetItem : public MenuItem {
  public:
    void render(char line[17], bool editing, const UiSnapshot&) const override {
        if (editing)
            snprintf(line, 17, "CLICK = CONFIRM ");
        else
            snprintf(line, 17, "%cWIFI RESET     ", UI_GLYPH_ARROW);
    }
    ClickResult on_click(const UiSnapshot&, UiOutput&) override {
        return ClickResult::EnterEdit;  // "CLICK = CONFIRM" armed
    }
    bool edit_step(int, const UiSnapshot&, UiOutput&) override {
        return false;  // rotate = cancel the confirm
    }
    bool edit_click(UiSnapshot&, UiOutput& out) override {
        out.emit(UiEffect::Type::WifiReset);
        return true;
    }
};

class ExitItem : public MenuItem {
  public:
    void render(char line[17], bool, const UiSnapshot&) const override {
        snprintf(line, 17, "%cEXIT           ", UI_GLYPH_ARROW);
    }
    ClickResult on_click(const UiSnapshot&, UiOutput&) override {
        return ClickResult::ExitMenu;
    }
};

BrightItem s_bright;
Use24hItem s_use24h;
TzItem s_tz;
CycleItem s_cycle;
WifiResetItem s_wifi_reset;
AboutItem s_about;
ExitItem s_exit;

// Rotation order per docs/DESIGN.md: BRIGHT, 24H, TZ, CYCLE, WIFI RESET,
// ABOUT, EXIT. test/native/test_ui_menu pins the order.
MenuItem* const s_items[] = {&s_bright,     &s_use24h, &s_tz,   &s_cycle,
                             &s_wifi_reset, &s_about,  &s_exit};

}  // namespace

MenuItem* const* ui_menu_items(uint8_t* count) {
    *count = sizeof(s_items) / sizeof(s_items[0]);
    return s_items;
}
