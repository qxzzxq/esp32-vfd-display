#ifndef ui_h
#define ui_h

// Page rendering + menu state machine. Sole owner of the VFD display.
// Never returns; call last from app_main.
void ui_run();

#endif
