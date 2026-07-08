#ifndef ui_pages_h
#define ui_pages_h

#include <stdint.h>

#include "page.h"

// Fixed page registry in rotation order: TIME, DATE, INDOOR, OUTDOOR, CUSTOM, PRESSURE.
// Index 0 (TIME) is always available — UiFsm's wrap-and-skip navigation
// relies on at least one available page.
UiPage* const* ui_pages(uint8_t* count);

// Registry index of CUSTOM — the FSM's jump target for a new message.
// The order above is pinned by test/native/test_ui_pages.
inline constexpr uint8_t UI_PAGE_CUSTOM = 4;

#endif
