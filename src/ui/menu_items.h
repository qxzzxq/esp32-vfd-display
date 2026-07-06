#ifndef ui_menu_items_h
#define ui_menu_items_h

#include <stdint.h>

#include "menu_item.h"

// Fixed menu registry in rotation order: BRIGHT, WIFI RESET, EXIT.
// M7 items (24H, TZ, CYCLE, STATUS) slot in here as new subclasses.
MenuItem* const* ui_menu_items(uint8_t* count);

#endif
