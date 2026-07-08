#ifndef weather_h
#define weather_h

#include <stdint.h>

// Outdoor weather from the Open-Meteo forecast API (no key, HTTPS).
// weather_fetch() runs from the worker task; the UI consumes the latest
// values via weather_get().

struct Weather {
    float tC;            // temperature_2m, deg C
    float rh;            // relative_humidity_2m, %
    float uv;            // uv_index
    int64_t fetched_us;  // esp_timer_get_time() at the last success (staleness)
};

// Blocking HTTPS GET + parse (seconds, 10 s timeout); call from the worker
// task only. lat/lon are the raw settings strings. Returns false on any
// transport, HTTP, or parse error (previous values are kept).
bool weather_fetch(const char* lat, const char* lon);

// Copies the last successful reading; false until the first success.
bool weather_get(Weather* out);

#endif
