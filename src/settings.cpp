#include "settings.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define NVS_NAMESPACE "vfdclk"

struct TzEntry {
    const char* name;   // <= 10 chars, shown in menu / portal
    const char* posix;
};

static const TzEntry TZ_TABLE[] = {
    {"UTC",       "UTC0"},
    {"LONDON",    "GMT0BST,M3.5.0/1,M10.5.0"},
    {"PARIS",     "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"BERLIN",    "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"NEW YORK",  "EST5EDT,M3.2.0,M11.1.0"},
    {"CHICAGO",   "CST6CDT,M3.2.0,M11.1.0"},
    {"DENVER",    "MST7MDT,M3.2.0,M11.1.0"},
    {"LOS ANGEL", "PST8PDT,M3.2.0,M11.1.0"},
    {"SHANGHAI",  "CST-8"},
    {"TOKYO",     "JST-9"},
    {"SYDNEY",    "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"AUCKLAND",  "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};

static Settings s_settings;
static SemaphoreHandle_t s_mutex;
static nvs_handle_t s_nvs;

const char* tz_name(uint8_t idx) { return TZ_TABLE[idx].name; }
const char* tz_posix(uint8_t idx) { return TZ_TABLE[idx].posix; }
int tz_count() { return sizeof(TZ_TABLE) / sizeof(TZ_TABLE[0]); }

static void apply_tz() {
    setenv("TZ", TZ_TABLE[s_settings.tz_idx].posix, 1);
    tzset();
}

static void load_str(const char* key, char* buf, size_t len) {
    size_t sz = len;
    nvs_get_str(s_nvs, key, buf, &sz);  // NOT_FOUND leaves the default in place
}

static void load_u8(const char* key, uint8_t* val) {
    nvs_get_u8(s_nvs, key, val);
}

void settings_init() {
    s_mutex = xSemaphoreCreateMutex();

    memset(&s_settings, 0, sizeof(s_settings));
    s_settings.bright = 200;
    s_settings.use24h = 1;

    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs));
    load_str("ssid", s_settings.ssid, sizeof(s_settings.ssid));
    load_str("pass", s_settings.pass, sizeof(s_settings.pass));
    load_u8("bright", &s_settings.bright);
    load_u8("use24h", &s_settings.use24h);
    load_u8("tz_idx", &s_settings.tz_idx);
    load_u8("cycle_s", &s_settings.cycle_s);
    load_str("lat", s_settings.lat, sizeof(s_settings.lat));
    load_str("lon", s_settings.lon, sizeof(s_settings.lon));
    load_str("msg", s_settings.msg, sizeof(s_settings.msg));

    if (s_settings.tz_idx >= tz_count()) s_settings.tz_idx = 0;
    apply_tz();
}

Settings settings_get() {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    Settings copy = s_settings;
    xSemaphoreGive(s_mutex);
    return copy;
}

void settings_save(const Settings& s) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_settings = s;
    // NVS skips the flash write when a value is unchanged, so writing all keys is cheap.
    nvs_set_str(s_nvs, "ssid", s_settings.ssid);
    nvs_set_str(s_nvs, "pass", s_settings.pass);
    nvs_set_u8(s_nvs, "bright", s_settings.bright);
    nvs_set_u8(s_nvs, "use24h", s_settings.use24h);
    nvs_set_u8(s_nvs, "tz_idx", s_settings.tz_idx);
    nvs_set_u8(s_nvs, "cycle_s", s_settings.cycle_s);
    nvs_set_str(s_nvs, "lat", s_settings.lat);
    nvs_set_str(s_nvs, "lon", s_settings.lon);
    nvs_set_str(s_nvs, "msg", s_settings.msg);
    nvs_commit(s_nvs);
    xSemaphoreGive(s_mutex);
    apply_tz();
}
