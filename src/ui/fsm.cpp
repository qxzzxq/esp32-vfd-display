// UI state machine. Transition rules and timing are ported verbatim from the
// pre-refactor src/ui.cpp main loop — test/native/test_ui_fsm pins them,
// including the deliberate quirks (release swallow, timeout anchored at
// button-down, hold-bar fill formula).

#include "fsm.h"

#include <stdio.h>

#include "pages.h"

void UiFsm::tick(UiInput in, int64_t now_us, const UiSnapshot& s, UiOutput* out) {
    out->line[0] = '\0';
    out->hold_fired = false;
    out->effect_count = 0;

    // Mutable per-tick view: a committing item writes its new value here so
    // the render below shows it immediately, one tick before the shell's
    // effect persists it and the next snapshot catches up.
    UiSnapshot view = s;

    if (in != UiInput::None) {
        last_input_us_ = now_us;
        switch (in) {
            case UiInput::StepCW: handle_step(1, view, *out); break;
            case UiInput::StepCCW: handle_step(-1, view, *out); break;
            case UiInput::BtnDown:
                press_start_us_ = now_us;
                break;
            case UiInput::BtnUp:
                if (press_start_us_ >= 0 && now_us - press_start_us_ < UI_LONG_PRESS_US)
                    handle_click(view, *out);
                // a full long-press already fired at the threshold below
                press_start_us_ = -1;
                break;
            default:
                break;
        }
    } else if (mode_ != Mode::Pages && now_us - last_input_us_ > UI_MENU_TIMEOUT_US) {
        // Inactivity: abandon the menu (and any uncommitted edit)
        abort_to_pages(view, *out);
    }

    // A new POST jumps the display to CUSTOM so the pushed message shows
    // (notification semantics). Only from the pages — the menu is never
    // interrupted, and the seq is consumed there so exiting the menu later
    // doesn't trigger a stale jump. A clearing POST (empty msg) never jumps.
    if (view.msg_seq != last_msg_seq_) {
        last_msg_seq_ = view.msg_seq;
        if (mode_ == Mode::Pages && view.msg[0] != '\0') page_ = UI_PAGE_CUSTOM;
    }

    // A page can lose availability while displayed (CUSTOM cleared via
    // POST /api/message). Advance past it before rendering; page 0 (TIME)
    // is always available, so the loop terminates.
    if (mode_ == Mode::Pages && !pages_[page_]->available(view)) {
        do {
            page_ = (uint8_t)((page_ + 1) % page_count_);
        } while (!pages_[page_]->available(view));
    }

    render(out->line, now_us, view);

    // Long-press fires while still held: from the pages it opens the menu,
    // from the menu it escapes. render() has just drawn the completed bar
    // (filled clamps at 9); hold_fired tells the shell to pause ~200 ms so it
    // registers before acting on the new mode. The eventual release is
    // swallowed via press_start_us_ = -1.
    if (press_start_us_ >= 0 && now_us - press_start_us_ >= UI_LONG_PRESS_US) {
        out->hold_fired = true;
        if (mode_ == Mode::Pages) {
            mode_ = Mode::Menu;
            item_ = 0;
        } else {
            abort_to_pages(view, *out);
        }
        press_start_us_ = -1;
    }
}

// Short-press action, dispatched on button release.
void UiFsm::handle_click(UiSnapshot& s, UiOutput& out) {
    switch (mode_) {
        case Mode::Pages:
            // menu entry is long-press (MENU bar); click on the pages is unassigned
            break;
        case Mode::Menu:
            switch (items_[item_]->on_click(s, out)) {
                case MenuItem::ClickResult::EnterEdit: mode_ = Mode::Edit; break;
                case MenuItem::ClickResult::ExitMenu: mode_ = Mode::Pages; break;
                case MenuItem::ClickResult::Stay: break;
            }
            break;
        case Mode::Edit:
            if (items_[item_]->edit_click(s, out)) mode_ = Mode::Menu;
            break;
    }
}

void UiFsm::handle_step(int dir, const UiSnapshot& s, UiOutput& out) {
    switch (mode_) {
        case Mode::Pages:
            // Wrap through the registry, skipping unavailable pages. Page 0
            // (TIME) is always available, so the loop terminates.
            do {
                page_ = (uint8_t)((page_ + dir + page_count_) % page_count_);
            } while (!pages_[page_]->available(s));
            break;
        case Mode::Menu:
            item_ = (uint8_t)((item_ + dir + item_count_) % item_count_);
            break;
        case Mode::Edit:
            if (!items_[item_]->edit_step(dir, s, out)) mode_ = Mode::Menu;
            break;
    }
}

void UiFsm::abort_to_pages(const UiSnapshot& s, UiOutput& out) {
    if (mode_ == Mode::Edit) items_[item_]->edit_abort(s, out);
    mode_ = Mode::Pages;
}

void UiFsm::render(char line[17], int64_t now_us, const UiSnapshot& s) const {
    // Overlay priority: hold bar, then portal banner, then content.
    if (press_start_us_ >= 0 && now_us - press_start_us_ >= UI_HOLD_SHOW_US) {
        render_hold_bar(line, now_us - press_start_us_);
    } else if (mode_ == Mode::Pages && s.net == UiNetState::Portal) {
        render_portal_banner(line, now_us, s);
    } else if (mode_ == Mode::Pages) {
        pages_[page_]->render(line, s, now_us);
    } else {
        items_[item_]->render(line, mode_ == Mode::Edit, s);
    }
}

void UiFsm::render_hold_bar(char line[17], int64_t held_us) const {
    // Hold progress bar (pages: enter menu; menu: exit), full at the threshold
    int filled = (int)((held_us - UI_HOLD_SHOW_US) * UI_HOLD_BAR_SEGS /
                       (UI_LONG_PRESS_US - UI_HOLD_SHOW_US));
    if (filled > UI_HOLD_BAR_SEGS) filled = UI_HOLD_BAR_SEGS;
    char bar[UI_HOLD_BAR_SEGS + 1];
    for (int i = 0; i < UI_HOLD_BAR_SEGS; i++) bar[i] = i < filled ? '=' : ' ';
    bar[UI_HOLD_BAR_SEGS] = '\0';
    // Label on the left, bracketed bar flush with the right edge; the
    // computed pad keeps the full 16 chars written for any segment count.
    snprintf(line, 17, "%-*s[%s]", 16 - (UI_HOLD_BAR_SEGS + 2),
             mode_ == Mode::Pages ? "MENU" : "EXIT", bar);
}

void UiFsm::render_portal_banner(char line[17], int64_t now_us, const UiSnapshot& s) {
    // Provisioning: alternate the AP name and the portal address every 3 s
    if ((now_us / 3000000LL) % 2 == 0)
        snprintf(line, 17, "SETUP  %-9s", s.ap_ssid);
    else
        snprintf(line, 17, "AP 192.168.4.1  ");
}
