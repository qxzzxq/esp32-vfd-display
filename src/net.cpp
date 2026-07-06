#include "net.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensors.h"
#include "settings.h"
#include "web.h"

static const char* TAG = "net";

static volatile NetState s_state = NetState::Connecting;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_ip[16] = "0.0.0.0";
static char s_ap_ssid[16] = "VFD-????";
static esp_timer_handle_t s_reconnect_timer;
static uint32_t s_backoff_ms = 1000;

// Polls producers; weather (15 min cadence) joins this loop at M5.
static void worker_task(void*) {
    while (1) {
        sensors_read();
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static void reconnect_cb(void*) {
    esp_wifi_connect();
}

static void wifi_event_handler(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_state = NetState::Connecting;
        ESP_LOGW(TAG, "disconnected, reconnecting in %lu ms", (unsigned long)s_backoff_ms);
        esp_timer_start_once(s_reconnect_timer, (uint64_t)s_backoff_ms * 1000);
        s_backoff_ms = s_backoff_ms >= 15000 ? 30000 : s_backoff_ms * 2;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* ev = (ip_event_got_ip_t*)data;
        taskENTER_CRITICAL(&s_lock);
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        taskEXIT_CRITICAL(&s_lock);
        s_backoff_ms = 1000;
        s_state = NetState::Connected;
        ESP_LOGI(TAG, "got ip %s", s_ip);
    }
}

static void start_sta() {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, nullptr));

    Settings st = settings_get();
    wifi_config_t wc = {};
    strlcpy((char*)wc.sta.ssid, st.ssid, sizeof(wc.sta.ssid));
    strlcpy((char*)wc.sta.password, st.pass, sizeof(wc.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Starts now, syncs once the network is up; retries internally.
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_cfg));
}

static void start_portal() {
    s_state = NetState::Portal;
    esp_netif_create_default_wifi_ap();  // 192.168.4.1
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "VFD-%02X%02X", mac[4], mac[5]);

    wifi_config_t wc = {};
    strlcpy((char*)wc.ap.ssid, s_ap_ssid, sizeof(wc.ap.ssid));
    wc.ap.ssid_len = strlen(s_ap_ssid);
    wc.ap.channel = 1;
    wc.ap.authmode = WIFI_AUTH_OPEN;
    wc.ap.max_connection = 4;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "portal AP \"%s\" at 192.168.4.1", s_ap_ssid);

    web_start_portal();
}

void net_init(bool force_portal) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (force_portal) {
        Settings st = settings_get();
        st.ssid[0] = '\0';
        st.pass[0] = '\0';
        settings_save(st);
        ESP_LOGW(TAG, "boot-hold: credentials erased, entering portal");
    }

    const esp_timer_create_args_t targs = {
        .callback = reconnect_cb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_retry",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_reconnect_timer));

    if (settings_get().ssid[0] == '\0')
        start_portal();
    else
        start_sta();

    xTaskCreate(worker_task, "worker", 6144, nullptr, 3, nullptr);
}

NetState net_state() {
    return s_state;
}

void net_get_ip(char out[16]) {
    taskENTER_CRITICAL(&s_lock);
    strcpy(out, s_ip);
    taskEXIT_CRITICAL(&s_lock);
}

void net_get_ap_ssid(char out[16]) {
    strcpy(out, s_ap_ssid);
}

void net_reset_credentials() {
    Settings st = settings_get();
    st.ssid[0] = '\0';
    st.pass[0] = '\0';
    settings_save(st);
    esp_restart();
}
