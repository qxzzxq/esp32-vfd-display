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
    // Page-transition animation: a page change starts a dimming crossfade; an
    // opted-in same-page content change (TIME second tick) rolls its changed
    // cells in lockstep. Composites the active animation into line/out->glyphs
    // and emits the crossfade's brightness effects. Only called for un-overlaid
    // Pages-mode frames; line holds the incoming target content on entry.
    void apply_transition(char line[17], int64_t now_us, uint8_t bright, UiOutput* out);
    void apply_fade(char line[17], int64_t now_us, uint8_t bright, UiOutput* out);
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
        int64_t start_us = 0;
        char from[17] = {};
    };
    RollState roll_;

    // Active page-transition crossfade, in two protected phases. Out dims the
    // outgoing page ('from') to black and always runs to completion — a page
    // change during it only retargets which page dims in. In dims the incoming
    // page (in line) back up and is interruptible — a page change restarts it
    // from black for the new page, so a fast scrub shows each page rising from
    // 0 rather than the previous one snapping back to full.
    struct FadeState {
        enum class Phase : uint8_t { Idle, Out, In };
        Phase phase = Phase::Idle;
        int64_t start_us = 0;  // start of the current phase
        char from[17] = {};    // outgoing page, shown throughout Out
    };
    FadeState fade_;
    uint8_t prev_page_ = 0xFF;     // 0xFF = no valid pages frame to animate from
    char prev_content_[17] = {};   // last Pages-mode logical (pre-animation) content
};

#endif
