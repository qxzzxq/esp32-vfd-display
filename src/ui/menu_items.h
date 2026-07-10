#ifndef ui_menu_items_h
#define ui_menu_items_h

#include <stdint.h>

#include "menu_item.h"

// Fixed menu registry in rotation order: BRIGHT, 24H, TZ, CYCLE, WIFI RESET,
// ABOUT, EXIT. Order is pinned by test/native/test_ui_menu.
MenuItem* const* ui_menu_items(uint8_t* count);

#endif
