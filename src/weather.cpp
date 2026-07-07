#include "weather.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char* TAG = "weather";

// Statically-initialized spinlock (same pattern as net.cpp's s_ip): the
// guarded copy is 20 bytes, and no init call is needed before weather_get().
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_valid;
static Weather s_weather;

// Response buffer is static (not stack) to spare the worker task's stack,
// which the in-task TLS handshake already leans on. Single caller by
// contract (worker task only), so no reentrancy concern.
static char s_body[1024];

// Fills *out from Open-Meteo's ~400 B response:
// {"current":{"temperature_2m":35.2,"relative_humidity_2m":15,"uv_index":8.65},...}
static bool parse_response(const char* body, Weather* out) {
    cJSON* root = cJSON_Parse(body);
    if (!root) return false;

    bool ok = false;
    cJSON* cur = cJSON_GetObjectItem(root, "current");
    if (cur) {
        cJSON* t = cJSON_GetObjectItem(cur, "temperature_2m");
        cJSON* rh = cJSON_GetObjectItem(cur, "relative_humidity_2m");
        cJSON* uv = cJSON_GetObjectItem(cur, "uv_index");
        if (cJSON_IsNumber(t) && cJSON_IsNumber(rh) && cJSON_IsNumber(uv)) {
            out->tC = (float)t->valuedouble;
            out->rh = (float)rh->valuedouble;
            out->uv = (float)uv->valuedouble;
            ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}

bool weather_fetch(const char* lat, const char* lon) {
    char url[192];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,relative_humidity_2m,uv_index",
             lat, lon);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 10000;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    bool ok = false;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open failed: %s", esp_err_to_name(err));
    } else {
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        int len = esp_http_client_read_response(client, s_body, sizeof(s_body) - 1);
        if (status != 200 || len <= 0) {
            ESP_LOGW(TAG, "http status %d, len %d", status, len);
        } else {
            s_body[len] = '\0';
            Weather w;
            if (!parse_response(s_body, &w)) {
                ESP_LOGW(TAG, "unexpected response: %.64s", s_body);
            } else {
                w.fetched_us = esp_timer_get_time();
                taskENTER_CRITICAL(&s_lock);
                s_weather = w;
                s_valid = true;
                taskEXIT_CRITICAL(&s_lock);
                ESP_LOGI(TAG, "%.1fC %.0f%% UV%.1f", w.tC, w.rh, w.uv);
                ok = true;
            }
        }
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
    return ok;
}

bool weather_get(Weather* out) {
    taskENTER_CRITICAL(&s_lock);
    bool valid = s_valid;
    if (valid) *out = s_weather;
    taskEXIT_CRITICAL(&s_lock);
    return valid;
}
