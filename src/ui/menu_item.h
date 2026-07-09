#ifndef ui_menu_item_h
#define ui_menu_item_h

#include <stdint.h>

#include "core.h"

// One settings-menu entry. Each item owns its full behavior — rendering in
// both highlighted and editing states, what a click does, and how rotation
// behaves while editing — so UiFsm needs almost no per-item knowledge (the
// lone exception is edit_subscreen, which lets ABOUT's pages crossfade).
//
// "Editing" is item-defined: a value edit for BRIGHT/24H/TZ/CYCLE, a
// confirm-armed state for WIFI RESET, a paged read-only overlay for ABOUT. The
// base implementations are the no-op item (EXIT-style): click does nothing
// special, edit ops immediately hand back to the menu.
class MenuItem {
  public:
    // What the FSM should do after a click on the highlighted item.
    enum class ClickResult : uint8_t { Stay, EnterEdit, ExitMenu };

    // Fill line with exactly 16 chars + NUL. editing selects the edit-state
    // rendering (">" cursor on the value side, confirm prompt, ...).
    virtual void render(char line[17], bool editing, const UiSnapshot& s) const = 0;

    // Click while highlighted in the menu. An item returning EnterEdit must
    // seed its working value here (the snapshot holds the saved state).
    virtual ClickResult on_click(const UiSnapshot&, UiOutput&) { return ClickResult::Stay; }

    // Rotation while editing, dir = +1/-1. Return false to cancel the edit
    // back to the menu (WIFI RESET: rotate = cancel the confirm).
    virtual bool edit_step(int, const UiSnapshot&, UiOutput&) { return true; }

    // Click while editing = commit. Return true to leave editing back to the
    // menu. The snapshot is the FSM's mutable per-tick view: a committing
    // item must write its new value into it so this tick already renders the
    // committed state — the persist effect only executes after the draw, and
    // rendering from the stale view would flash the previous value.
    virtual bool edit_click(UiSnapshot&, UiOutput&) { return true; }

    // Edit abandoned (menu timeout or long-press exit): undo any transient
    // side effects (BRIGHT: restore the saved brightness). Nothing to persist.
    virtual void edit_abort(const UiSnapshot&, UiOutput&) {}

    // Sub-screen index while editing; folded into UiFsm::screen_id so an item
    // with several edit pages crossfades between them (ABOUT: IP vs version).
    // Default 0 keeps value edits (BRIGHT/TZ/CYCLE) on the menu item's screen id
    // so entering edit and previewing a value never fade.
    virtual uint8_t edit_subscreen() const { return 0; }

  protected:
    ~MenuItem() = default;  // never deleted via base pointer
};

#endif
