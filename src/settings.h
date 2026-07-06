#ifndef settings_h
#define settings_h

#include <stdint.h>

// Device settings persisted in NVS (namespace "vfdclk"). See docs/DESIGN.md.
struct Settings {
    char ssid[33];
    char pass[65];
    uint8_t bright;   // VFD brightness, driver units 0-240
    uint8_t use24h;   // 1 = 24h clock
    uint8_t tz_idx;   // index into the timezone table
    uint8_t cycle_s;  // auto-cycle interval in seconds, 0 = off
    char lat[12];     // weather location; empty = weather disabled
    char lon[12];
    char msg[65];     // custom page text (POST API)
};

// Loads from NVS (defaults for missing keys) and applies the timezone.
// Requires nvs_flash_init() to have run.
void settings_init();
Settings settings_get();                 // copy under mutex
void settings_save(const Settings& s);   // persist + re-apply timezone

// Timezone table (display name <= 10 chars, POSIX TZ string).
const char* tz_name(uint8_t idx);
const char* tz_posix(uint8_t idx);
int tz_count();

#endif
