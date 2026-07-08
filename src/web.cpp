#include "web.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "net.h"
#include "sensors.h"
#include "settings.h"
#include "weather.h"

static const char* TAG = "web";

static const char PORTAL_HTML_HEAD[] = R"(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>VFD Clock Setup</title>
<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}
label{display:block;margin-top:1em}input,select{width:100%;padding:.5em;box-sizing:border-box}
button{margin-top:1.5em;width:100%;padding:.7em}</style></head><body>
<h2>VFD Clock Setup</h2><form method="POST" action="/save">
<label>WiFi network<input name="ssid" maxlength="32" required></label>
<label>Password<input name="pass" type="password" minlength="8" maxlength="64"></label>
<label>Timezone<select name="tz">)";

static const char PORTAL_HTML_TAIL[] = R"(</select></label>
<label>Latitude (optional, for weather)<input name="lat" maxlength="11"></label>
<label>Longitude (optional)<input name="lon" maxlength="11"></label>
<button type="submit">Save &amp; Reboot</button></form></body></html>)";

// In-place decode of application/x-www-form-urlencoded values.
static void url_decode(char* s) {
    char* o = s;
    for (char* p = s; *p; p++) {
        if (*p == '+') {
            *o++ = ' ';
        } else if (*p == '%' && p[1] && p[2]) {
            char hex[3] = {p[1], p[2], '\0'};
            *o++ = (char)strtol(hex, nullptr, 16);
            p += 2;
        } else {
            *o++ = *p;
        }
    }
    *o = '\0';
}

static esp_err_t root_get_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, PORTAL_HTML_HEAD, HTTPD_RESP_USE_STRLEN);
    uint8_t cur_tz = settings_get().tz_idx;
    char opt[64];
    for (int i = 0; i < tz_count(); i++) {
        snprintf(opt, sizeof(opt), "<option value=\"%d\"%s>%s</option>", i,
                 i == cur_tz ? " selected" : "", tz_name(i));
        httpd_resp_send_chunk(req, opt, HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_send_chunk(req, PORTAL_HTML_TAIL, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

// Copies a decoded form field into dst; leaves dst untouched when absent.
// Returns false when the decoded value is too long for dst.
static bool form_field(const char* body, const char* key, char* dst, size_t dst_len) {
    char val[384];  // sized for the URL-encoded value, which can be up to the whole body
    if (httpd_query_key_value(body, key, val, sizeof(val)) != ESP_OK) return true;
    url_decode(val);
    if (strlen(val) >= dst_len) return false;
    strlcpy(dst, val, dst_len);
    return true;
}

static esp_err_t save_post_handler(httpd_req_t* req) {
    char body[384];
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad request length");
        return ESP_FAIL;
    }
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, body + got, len - got);
        if (r <= 0) return ESP_FAIL;
        got += r;
    }
    body[len] = '\0';

    Settings st = settings_get();
    char tz[8] = "";
    if (!form_field(body, "ssid", st.ssid, sizeof(st.ssid)) ||
        !form_field(body, "pass", st.pass, sizeof(st.pass)) ||
        !form_field(body, "lat", st.lat, sizeof(st.lat)) ||
        !form_field(body, "lon", st.lon, sizeof(st.lon)) ||
        !form_field(body, "tz", tz, sizeof(tz))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "value too long");
        return ESP_FAIL;
    }
    if (tz[0]) {
        int idx = atoi(tz);
        if (idx >= 0 && idx < tz_count()) st.tz_idx = (uint8_t)idx;
    }
    if (st.ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }
    size_t pass_len = strlen(st.pass);
    if (pass_len > 0 && pass_len < 8) {
        // esp_wifi_set_config() rejects 1-7 char passwords; saving one would
        // reboot into an aborting STA startup.
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "password must be 8+ chars");
        return ESP_FAIL;
    }
    settings_save(st);
    ESP_LOGI(TAG, "credentials saved for \"%s\", rebooting", st.ssid);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<html><body><h2>Saved. The clock is rebooting and will join your "
        "WiFi.</h2></body></html>");
    vTaskDelay(pdMS_TO_TICKS(800));  // let the response flush
    esp_restart();
    return ESP_OK;
}

// Catches captive-portal probes (generate_204, hotspot-detect.html, ...).
static esp_err_t redirect_handler(httpd_req_t* req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

// Minimal DNS responder: answers every A query with 192.168.4.1 so any
// hostname a client tries lands on the portal.
static void dns_task(void*) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(sock, (struct sockaddr*)&addr, sizeof(addr));

    uint8_t buf[320];
    while (1) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        int len = recvfrom(sock, buf, sizeof(buf) - 16, 0, (struct sockaddr*)&src, &slen);
        if (len < 12) continue;
        // Walk the question name so the answer lands right after the question
        // section, dropping any additional records (e.g. EDNS0) the client sent.
        int q = 12;
        while (q < len && buf[q] != 0) q += buf[q] + 1;
        q += 5;  // name terminator + QTYPE/QCLASS
        if (q > len) continue;
        buf[2] = 0x81;  // response, recursion desired
        buf[3] = 0x80;  // recursion available, no error
        buf[6] = 0;  // ANCOUNT = 1 (single question in practice)
        buf[7] = 1;
        buf[8] = buf[9] = buf[10] = buf[11] = 0;  // no NS/AR records
        static const uint8_t ans[16] = {
            0xC0, 0x0C,              // name: pointer to the query name
            0x00, 0x01, 0x00, 0x01,  // type A, class IN
            0x00, 0x00, 0x00, 0x3C,  // TTL 60 s
            0x00, 0x04, 192, 168, 4, 1,
        };
        memcpy(buf + q, ans, sizeof(ans));
        sendto(sock, buf, q + sizeof(ans), 0, (struct sockaddr*)&src, slen);
    }
}

void web_start_portal() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    httpd_handle_t server = nullptr;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));

    static const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = nullptr};
    static const httpd_uri_t save = {
        .uri = "/save", .method = HTTP_POST, .handler = save_post_handler, .user_ctx = nullptr};
    static const httpd_uri_t wild = {
        .uri = "/*", .method = HTTP_GET, .handler = redirect_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &save);
    httpd_register_uri_handler(server, &wild);

    xTaskCreate(dns_task, "dns", 3072, nullptr, 3, nullptr);
    ESP_LOGI(TAG, "portal http server + dns hijack up");
}

// ---- STA-mode JSON API ------------------------------------------------

static httpd_handle_t s_api = nullptr;

// JSON error response. Returns ESP_FAIL so httpd closes the connection,
// which also discards the unread body on the 413 path.
static esp_err_t json_error(httpd_req_t* req, const char* status, const char* msg) {
    char buf[96];  // msg is always a fixed ASCII literal — no escaping needed
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_FAIL;
}

// Sends a built cJSON tree and frees it.
static esp_err_t send_json(httpd_req_t* req, cJSON* root) {
    char* out = root ? cJSON_PrintUnformatted(root) : nullptr;
    cJSON_Delete(root);
    if (!out) return json_error(req, "500 Internal Server Error", "out of memory");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    cJSON_free(out);
    return ESP_OK;
}

// cJSON prints raw floats with up to 15 significant digits of promotion
// noise ("23.3999996185303"); round to the one decimal the display shows.
static void add_num1(cJSON* o, const char* k, float v) {
    cJSON_AddNumberToObject(o, k, round((double)v * 10.0) / 10.0);
}

static esp_err_t msg_post_handler(httpd_req_t* req) {
    if (req->content_len > 256)
        return json_error(req, "413 Content Too Large", "body too large");
    int len = req->content_len;
    if (len <= 0) return json_error(req, "400 Bad Request", "empty body");
    char body[257];
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, body + got, len - got);
        if (r <= 0) return ESP_FAIL;
        got += r;
    }
    body[len] = '\0';

    cJSON* root = cJSON_Parse(body);
    if (!root) return json_error(req, "400 Bad Request", "invalid JSON");
    cJSON* text = cJSON_GetObjectItem(root, "text");
    if (!cJSON_IsString(text)) {
        cJSON_Delete(root);
        return json_error(req, "400 Bad Request", "text must be a string");
    }
    size_t n = strlen(text->valuestring);
    if (n > 64) {
        cJSON_Delete(root);
        return json_error(req, "400 Bad Request", "text too long (max 64)");
    }
    for (size_t i = 0; i < n; i++) {
        // Unsigned compare also rejects UTF-8 bytes and controls smuggled
        // in as \u escapes (cJSON unescapes them to raw bytes).
        unsigned char c = (unsigned char)text->valuestring[i];
        if (c < 0x20 || c > 0x7E) {
            cJSON_Delete(root);
            return json_error(req, "400 Bad Request", "text must be printable ASCII");
        }
    }

    Settings st = settings_get();
    strlcpy(st.msg, text->valuestring, sizeof(st.msg));
    cJSON_Delete(root);
    settings_save(st);  // empty string clears; the CUSTOM page follows msg
    ESP_LOGI(TAG, "message set (%d chars)", (int)n);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t msg_get_handler(httpd_req_t* req) {
    Settings st = settings_get();
    cJSON* root = cJSON_CreateObject();
    if (root && !cJSON_AddStringToObject(root, "text", st.msg)) {
        cJSON_Delete(root);
        root = nullptr;
    }
    return send_json(req, root);  // cJSON escapes any " or \ in the text
}

static esp_err_t status_get_handler(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    if (!root) return send_json(req, nullptr);

    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm_now);
    cJSON_AddStringToObject(root, "time", ts);
    cJSON_AddBoolToObject(root, "synced", net_time_synced());

    float tC, rh, hPa;
    if (sensors_get(&tC, &rh, &hPa)) {
        cJSON* in = cJSON_AddObjectToObject(root, "indoor");
        if (in) {
            add_num1(in, "t", tC);
            add_num1(in, "rh", rh);
            add_num1(in, "p", hPa);  // NAN → null when the BMP280 is absent
        }
    } else {
        cJSON_AddNullToObject(root, "indoor");
    }

    Weather w;
    if (weather_get(&w)) {
        cJSON* wo = cJSON_AddObjectToObject(root, "weather");
        if (wo) {
            add_num1(wo, "t", w.tC);
            add_num1(wo, "rh", w.rh);
            add_num1(wo, "uv", w.uv);
            cJSON_AddNumberToObject(
                wo, "age_s", (double)((esp_timer_get_time() - w.fetched_us) / 1000000));
        }
    } else {
        cJSON_AddNullToObject(root, "weather");
    }

    int8_t rssi;
    if (net_get_rssi(&rssi))
        cJSON_AddNumberToObject(root, "rssi", rssi);
    else
        cJSON_AddNullToObject(root, "rssi");
    cJSON_AddNumberToObject(root, "heap", (double)esp_get_free_heap_size());

    return send_json(req, root);
}

void web_start_api() {
    if (s_api) return;  // GOT_IP re-fires on reconnect; the server survives drops
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();  // default exact URI matching
    ESP_ERROR_CHECK(httpd_start(&s_api, &cfg));

    static const httpd_uri_t msg_post = {.uri = "/api/message",
                                         .method = HTTP_POST,
                                         .handler = msg_post_handler,
                                         .user_ctx = nullptr};
    static const httpd_uri_t msg_get = {.uri = "/api/message",
                                        .method = HTTP_GET,
                                        .handler = msg_get_handler,
                                        .user_ctx = nullptr};
    static const httpd_uri_t status_get = {.uri = "/api/status",
                                           .method = HTTP_GET,
                                           .handler = status_get_handler,
                                           .user_ctx = nullptr};
    httpd_register_uri_handler(s_api, &msg_post);
    httpd_register_uri_handler(s_api, &msg_get);
    httpd_register_uri_handler(s_api, &status_get);
    ESP_LOGI(TAG, "api server up");
}
