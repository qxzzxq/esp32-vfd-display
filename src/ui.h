#ifndef ui_h
#define ui_h

// Thin shell over the pure UI core in src/ui/ (UiFsm + pages + menu items).
// Sole owner of the VFD display. Never returns; call last from app_main.
void ui_run();

#endif
