#include "encoder.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sensors.h"
#include "settings.h"
#include "ui.h"

static const char* TAG = "main";

// Temporary M3 plumbing: polls the sensors every 10 s. Folds into the net
// worker task when networking lands (M4).
static void sensor_poll_task(void*) {
  while (1) {
    float t, rh, hPa;
    if (sensors_read() && sensors_get(&t, &rh, &hPa))
      ESP_LOGI(TAG, "indoor %.1fC %.0f%% %.1fhPa", t, rh, hPa);
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

// Boot sequence per docs/DESIGN.md. Networking (M4) slots in before ui_run.
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
  xTaskCreate(sensor_poll_task, "sensors", 4096, nullptr, 3, nullptr);
  ui_run();  // never returns
}
