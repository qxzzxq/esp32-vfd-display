// Concrete menu items. Rendering and edit behavior are ported verbatim from
// the pre-refactor src/ui.cpp (render()'s menu switch, handle_click,
// handle_step) — test/native/test_ui_menu pins the behavior.

#include "menu_items.h"

#include <stdio.h>

namespace {

// Brightness value edit: menu units 1..15, driver = x16. Rotation previews
// live (display only); the commit click persists.
class BrightItem : public MenuItem {
  public:
    void render(char line[17], bool editing, const UiSnapshot& s) const override {
        // Edit mode is indicated by the ">" cursor moving to the value side
        if (editing) {
            char v[8];
            snprintf(v, sizeof(v), ">%d", val_);
            snprintf(line, 17, " BRIGHT %8s", v);
        } else {
            snprintf(line, 17, ">BRIGHT %8d", s.bright / 16);
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
    bool edit_click(const UiSnapshot&, UiOutput& out) override {
        out.emit(UiEffect::Type::CommitBrightness, (uint8_t)(val_ * 16));
        return true;
    }
    void edit_abort(const UiSnapshot& s, UiOutput& out) override {
        out.emit(UiEffect::Type::SetBrightness, s.bright);  // undo the preview
    }

  private:
    int val_ = 1;  // menu units 1..15; reseeded by on_click on every edit entry
};

// Two-step credential erase: click arms the confirm, rotate cancels, a
// second click fires (the shell reboots into the portal).
class WifiResetItem : public MenuItem {
  public:
    void render(char line[17], bool editing, const UiSnapshot&) const override {
        if (editing)
            snprintf(line, 17, "CLICK = CONFIRM ");
        else
            snprintf(line, 17, ">WIFI RESET     ");
    }
    ClickResult on_click(const UiSnapshot&, UiOutput&) override {
        return ClickResult::EnterEdit;  // "CLICK = CONFIRM" armed
    }
    bool edit_step(int, const UiSnapshot&, UiOutput&) override {
        return false;  // rotate = cancel the confirm
    }
    bool edit_click(const UiSnapshot&, UiOutput& out) override {
        out.emit(UiEffect::Type::WifiReset);
        return true;
    }
};

class ExitItem : public MenuItem {
  public:
    void render(char line[17], bool, const UiSnapshot&) const override {
        snprintf(line, 17, ">EXIT           ");
    }
    ClickResult on_click(const UiSnapshot&, UiOutput&) override {
        return ClickResult::ExitMenu;
    }
};

BrightItem s_bright;
WifiResetItem s_wifi_reset;
ExitItem s_exit;

MenuItem* const s_items[] = {&s_bright, &s_wifi_reset, &s_exit};

}  // namespace

MenuItem* const* ui_menu_items(uint8_t* count) {
    *count = sizeof(s_items) / sizeof(s_items[0]);
    return s_items;
}
