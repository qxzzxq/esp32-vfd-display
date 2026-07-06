#include "encoder.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "settings.h"
#include "ui.h"

// Boot sequence per docs/DESIGN.md. Sensors (M3) and networking (M4) slot in
// between encoder_init and ui_run as those milestones land.
extern "C" void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  settings_init();
  encoder_init();
  ui_run();  // never returns
}
