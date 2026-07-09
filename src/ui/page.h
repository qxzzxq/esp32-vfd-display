#ifndef ui_page_h
#define ui_page_h

#include <stdint.h>

#include "core.h"

// One display page (TIME, DATE, ...). Concrete pages live in pages.cpp as
// static singletons; UiFsm navigates a fixed array of them and only calls
// these two ops.
class UiPage {
  public:
    // Fill line with exactly 16 chars + NUL from the snapshot. now_us is the
    // monotonic clock for time-based animation (CUSTOM marquee: window offset =
    // (now_us / 300 ms) % (len + 3), sliding over the text plus a 3-space
    // wrap gap — stateless).
    virtual void render(char line[17], const UiSnapshot& s, int64_t now_us) const = 0;

    // Whether the page appears in rotation right now (PRESSURE only exists
    // when a BMP280 was probed; CUSTOM only when a message is set).
    virtual bool available(const UiSnapshot&) const { return true; }

    // Opt-in to the vertical roll when this page's content changes while it
    // is displayed (TIME: digits roll every second). Off by default so value
    // jitter (sensors) and the CUSTOM marquee don't animate.
    virtual bool rolls_on_change() const { return false; }

  protected:
    ~UiPage() = default;  // never deleted via base pointer
};

#endif
