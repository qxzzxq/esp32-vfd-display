#ifndef ui_pages_h
#define ui_pages_h

#include <stdint.h>

#include "page.h"

// Fixed page registry in rotation order: TIME, DATE, INDOOR, OUTDOOR, PRESSURE.
// Index 0 (TIME) is always available — UiFsm's wrap-and-skip navigation
// relies on at least one available page.
UiPage* const* ui_pages(uint8_t* count);

#endif
