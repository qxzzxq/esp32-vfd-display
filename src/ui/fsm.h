#ifndef ui_fsm_h
#define ui_fsm_h

#include <stdint.h>

#include "core.h"
#include "menu_item.h"
#include "page.h"

// The UI state machine: modes (Pages / Menu / Edit), gesture recognition
// (click vs long-press with hold bar) from raw button edges, menu inactivity
// timeout, and the render priority chain (hold bar > portal banner >
// page/menu content).
//
// Pure and deterministic: one tick() call consumes one input (UiInput::None
// on the ~100 ms idle render tick) at a monotonic timestamp against a data
// snapshot, and fills a UiOutput — the exact 16-char line to draw plus any
// effects for the shell to execute. Page/menu registries are injected so
// tests can use the real ones (or fakes).
class UiFsm {
  public:
    UiFsm(UiPage* const* pages, uint8_t page_count,
          MenuItem* const* items, uint8_t item_count)
        : pages_(pages), page_count_(page_count), items_(items), item_count_(item_count) {}

    // Process one input at monotonic time now_us. *out is fully rewritten.
    // Internally works on a copy of s: a committing menu item updates that
    // view so the tick's render already shows the committed value (the
    // persist effect only executes after the shell draws the line).
    void tick(UiInput in, int64_t now_us, const UiSnapshot& s, UiOutput* out);

  private:
    enum class Mode : uint8_t { Pages, Menu, Edit };

    void handle_click(UiSnapshot& s, UiOutput& out);
    void handle_step(int dir, const UiSnapshot& s, UiOutput& out);
    // Leave Menu/Edit for the pages, letting an active edit undo its
    // transient side effects. Single home for what the pre-refactor code
    // duplicated across the timeout, long-press, and EXIT paths.
    void abort_to_pages(const UiSnapshot& s, UiOutput& out);
    void render(char line[17], int64_t now_us, const UiSnapshot& s) const;
    void render_hold_bar(char line[17], int64_t held_us) const;
    static void render_portal_banner(char line[17], int64_t now_us, const UiSnapshot& s);
    // Roll animation: detect triggers (page change or opted-in content
    // change — all changed cells roll in lockstep), then composite the
    // active roll into line/out->glyphs. Only called for un-overlaid
    // Pages-mode frames; line holds the target content on entry.
    void apply_roll(char line[17], int64_t now_us, UiOutput* out);
    static void default_glyphs(UiOutput* out);

    UiPage* const* pages_;
    uint8_t page_count_;
    MenuItem* const* items_;
    uint8_t item_count_;

    Mode mode_ = Mode::Pages;
    uint8_t page_ = 0;
    uint8_t item_ = 0;
    int64_t last_input_us_ = 0;   // drives the menu inactivity timeout
    int64_t press_start_us_ = -1; // -1 = button idle (or release swallowed)
    uint32_t last_msg_seq_ = 0;   // detects new POSTs (jump to CUSTOM)

    // Active roll: 'from' is frozen at trigger time; the target is recomputed
    // live each tick, so mid-flight content changes just retarget.
    struct RollState {
        bool active = false;
        bool upward = true;
        int steps = UI_ROLL_STEPS;  // UI_PAGE_ROLL_STEPS for page changes
        int64_t start_us = 0;
        char from[17] = {};
    };
    RollState roll_;
    int dir_hint_ = 1;             // set by handle_step, consumed by apply_roll
    uint8_t prev_page_ = 0xFF;     // 0xFF = no valid pages frame to roll from
    char prev_content_[17] = {};   // last Pages-mode logical (pre-roll) content
};

#endif
