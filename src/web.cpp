#include "web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "settings.h"

static const char* TAG = "web";

static const char PORTAL_HTML_HEAD[] = R"(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>VFD Clock Setup</title>
<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}
label{display:block;margin-top:1em}input,select{width:100%;padding:.5em;box-sizing:border-box}
button{margin-top:1.5em;width:100%;padding:.7em}</style></head><body>
<h2>VFD Clock Setup</h2><form method="POST" action="/save">
<label>WiFi network<input name="ssid" maxlength="32" required></label>
<label>Password<input name="pass" type="password" maxlength="64"></label>
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
static void form_field(const char* body, const char* key, char* dst, size_t dst_len) {
    char val[65];
    if (httpd_query_key_value(body, key, val, sizeof(val)) == ESP_OK) {
        url_decode(val);
        strlcpy(dst, val, dst_len);
    }
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
    form_field(body, "ssid", st.ssid, sizeof(st.ssid));
    form_field(body, "pass", st.pass, sizeof(st.pass));
    form_field(body, "lat", st.lat, sizeof(st.lat));
    form_field(body, "lon", st.lon, sizeof(st.lon));
    char tz[8] = "";
    form_field(body, "tz", tz, sizeof(tz));
    if (tz[0]) {
        int idx = atoi(tz);
        if (idx >= 0 && idx < tz_count()) st.tz_idx = (uint8_t)idx;
    }
    if (st.ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
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
        buf[2] = 0x81;  // response, recursion desired
        buf[3] = 0x80;  // recursion available, no error
        buf[6] = buf[4];  // ANCOUNT = QDCOUNT (single question in practice)
        buf[7] = buf[5];
        buf[8] = buf[9] = buf[10] = buf[11] = 0;  // no NS/AR records
        static const uint8_t ans[16] = {
            0xC0, 0x0C,              // name: pointer to the query name
            0x00, 0x01, 0x00, 0x01,  // type A, class IN
            0x00, 0x00, 0x00, 0x3C,  // TTL 60 s
            0x00, 0x04, 192, 168, 4, 1,
        };
        memcpy(buf + len, ans, sizeof(ans));
        sendto(sock, buf, len + sizeof(ans), 0, (struct sockaddr*)&src, slen);
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
