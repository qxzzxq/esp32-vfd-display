#include "driver/gpio.h"
#include "encoder.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "nvs_flash.h"
#include "sensors.h"
#include "settings.h"
#include "ui.h"

static const char* TAG = "main";

// Encoder SW held low for >=3 s at boot = wipe WiFi creds, re-enter portal.
// The pin (pull-up) is configured by encoder_init before this runs.
static bool boot_hold_check() {
  if (gpio_get_level(GPIO_NUM_20)) return false;
  for (int i = 0; i < 30; i++) {
    vTaskDelay(pdMS_TO_TICKS(100));
    if (gpio_get_level(GPIO_NUM_20)) return false;
  }
  ESP_LOGW(TAG, "boot-hold detected");
  return true;
}

// Boot sequence per docs/DESIGN.md.
extern "C" void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  settings_init();
  encoder_init();
  sensors_init();
  net_init(boot_hold_check());  // also starts the sensor/weather worker task
  ui_run();                     // never returns
}
