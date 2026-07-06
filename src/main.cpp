#include <stdio.h>

#include "VFDDisplay.h"
#include "encoder.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Define pins
#define VFD_CS  6
#define VFD_CLK 7
#define VFD_DIN 21

static const char* TAG = "main";

// Create VFD display object with 16 digits (uses SPI2_HOST by default)
VFDDisplay vfd(VFD_CS, VFD_CLK, VFD_DIN, 16);

// M1 milestone test loop: log every encoder event and mirror it on the VFD.
// Verify: exactly one step per detent (no bounce/reversals), and click vs
// long-press (>=1.5 s) distinguishable by the logged hold duration.
extern "C" void app_main(void) {
  vfd.init();
  vfd.clear();
  vfd.setBrightness(120);
  vfd.writeString(0, "ENCODER TEST    ");

  encoder_init();
  ESP_LOGI(TAG, "encoder test ready: rotate and click");

  int pos = 0;
  int64_t btn_down_us = 0;
  char line[17];
  while (1) {
    EncEvent ev;
    if (!encoder_wait(&ev, portMAX_DELAY)) continue;

    switch (ev) {
      case EncEvent::StepCW:
        pos++;
        ESP_LOGI(TAG, "CW  pos=%d", pos);
        snprintf(line, sizeof(line), "CW   POS %6d", pos);
        break;
      case EncEvent::StepCCW:
        pos--;
        ESP_LOGI(TAG, "CCW pos=%d", pos);
        snprintf(line, sizeof(line), "CCW  POS %6d", pos);
        break;
      case EncEvent::BtnDown:
        btn_down_us = esp_timer_get_time();
        ESP_LOGI(TAG, "BTN down");
        snprintf(line, sizeof(line), "BTN DOWN        ");
        break;
      case EncEvent::BtnUp: {
        int64_t held_ms = (esp_timer_get_time() - btn_down_us) / 1000;
        ESP_LOGI(TAG, "BTN up after %lld ms (%s)", held_ms,
                 held_ms >= 1500 ? "LONG-PRESS" : "CLICK");
        snprintf(line, sizeof(line), "%-5s %7lldMS",
                 held_ms >= 1500 ? "LONG" : "CLICK", held_ms);
        break;
      }
    }
    vfd.writeString(0, line);
  }
}
