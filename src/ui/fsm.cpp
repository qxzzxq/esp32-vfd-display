// UI state machine. Transition rules and timing are ported verbatim from the
// pre-refactor src/ui.cpp main loop — test/native/test_ui_fsm pins them,
// including the deliberate quirks (release swallow, timeout anchored at
// button-down, hold-bar fill formula).

#include "fsm.h"

#include <stdio.h>
#include <string.h>

#include "glyphs.h"
#include "pages.h"
#include "roll.h"

void UiFsm::tick(UiInput in, int64_t now_us, const UiSnapshot& s, UiOutput* out) {
    out->line[0] = '\0';
    out->hold_fired = false;
    out->animating = false;
    out->effect_count = 0;
    default_glyphs(out);

    // Mutable per-tick view: a committing item writes its new value here so
    // the render below shows it immediately, one tick before the shell's
    // effect persists it and the next snapshot catches up.
    UiSnapshot view = s;

    if (in != UiInput::None) {
        // Any input snaps an in-flight roll to its target before dispatch,
        // so what the user acts on is what the display settles to.
        roll_.active = false;
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

    // Roll only un-overlaid Pages-mode content; any other frame (menu, hold
    // bar, portal banner) invalidates the roll-from state so the first pages
    // frame afterwards snaps instead of animating from stale content.
    bool hold_visible =
        press_start_us_ >= 0 && now_us - press_start_us_ >= UI_HOLD_SHOW_US;
    if (mode_ == Mode::Pages && !hold_visible && view.net != UiNetState::Portal) {
        apply_roll(out->line, now_us, out);
    } else {
        roll_.active = false;
        prev_page_ = 0xFF;
    }
    out->animating = roll_.active || hold_visible;

    // Long-press fires while still held: from the pages it opens the menu,
    // from the menu it escapes. render() has just drawn the completed bar
    // (the fill clamps at full); hold_fired tells the shell to pause ~200 ms so it
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
            dir_hint_ = dir;  // the roll drum follows the knob direction
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
    // Hold progress bar (pages: enter menu; menu: exit), full at the
    // threshold. Column-granular: 5 cells x 5 columns; partial cells use the
    // CGRAM bar glyphs (code == lit columns), full cells the CGROM block.
    int cols = (int)((held_us - UI_HOLD_SHOW_US) * (UI_HOLD_BAR_SEGS * 5) /
                     (UI_LONG_PRESS_US - UI_HOLD_SHOW_US));
    char bar[UI_HOLD_BAR_SEGS + 1];
    for (int i = 0; i < UI_HOLD_BAR_SEGS; i++) {
        int lit = cols - 5 * i;
        if (lit < 0) lit = 0;
        if (lit > 5) lit = 5;
        bar[i] = lit == 0 ? ' ' : lit == 5 ? UI_GLYPH_BAR_FULL : (char)lit;
    }
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

void UiFsm::default_glyphs(UiOutput* out) {
    memset(out->glyphs, 0, sizeof(out->glyphs));
    for (int i = 0; i < UI_GLYPH_COUNT; i++)
        memcpy(out->glyphs[UI_GLYPHS[i].slot], UI_GLYPHS[i].cols, 5);
}

void UiFsm::apply_roll(char line[17], int64_t now_us, UiOutput* out) {
    // Trigger detection against the previous pages frame. A page change
    // (rotation, msg-jump, availability auto-advance, future auto-cycle)
    // starts a staggered wave; an opted-in same-page content change (TIME
    // second tick) rolls its changed cells in lockstep.
    if (prev_page_ != 0xFF) {
        if (page_ != prev_page_) {
            roll_.active = true;
            roll_.wave = true;
            roll_.upward = dir_hint_ >= 0;
            roll_.start_us = now_us;
            memcpy(roll_.from, prev_content_, 17);
        } else if (!roll_.active && pages_[page_]->rolls_on_change() &&
                   strcmp(line, prev_content_) != 0) {
            roll_.active = true;
            roll_.wave = false;
            roll_.upward = true;
            roll_.start_us = now_us;
            memcpy(roll_.from, prev_content_, 17);
        }
    }
    dir_hint_ = 1;
    prev_page_ = page_;
    memcpy(prev_content_, line, 17);  // always the logical target
    if (!roll_.active) return;

    // Composite the in-flight cells over the target. Slots are reallocated
    // left->right every frame (stateless; the shell diff-uploads changes).
    // If more cells are mid-roll than slots — only reachable by lockstep
    // rolls with > 7 diffs, e.g. a 24H format flip — the excess snaps.
    int64_t elapsed = now_us - roll_.start_us;
    bool done = true;
    int ordinal = 0;   // index among changed cells: fixes the wave schedule
    int next_slot = 1;
    for (int i = 0; i < 16; i++) {
        if (roll_.from[i] == line[i]) continue;
        int64_t cell_elapsed =
            elapsed - (roll_.wave ? ordinal * UI_WAVE_STAGGER_US : 0);
        ordinal++;
        int k = cell_elapsed <= 0 ? 0 : (int)(cell_elapsed / UI_ROLL_STEP_US);
        if (k >= UI_ROLL_STEPS) continue;  // this cell already shows the target
        done = false;
        if (k == 0) {
            line[i] = roll_.from[i];  // scheduled but not moving yet
        } else if (next_slot <= 7) {
            ui_roll_composite(roll_.from[i], line[i], k, roll_.upward,
                              out->glyphs[next_slot]);
            line[i] = (char)next_slot++;
        }
        // else: over budget — leave the target char (snap)
    }
    if (done) roll_.active = false;
}
