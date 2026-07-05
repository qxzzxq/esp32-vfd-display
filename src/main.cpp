#include "VFDDisplay.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Define pins
#define VFD_CS  6
#define VFD_CLK 7
#define VFD_DIN 21

// Create VFD display object with 16 digits (uses SPI2_HOST by default)
VFDDisplay vfd(VFD_CS, VFD_CLK, VFD_DIN, 16);

// For 8 digits, you would use:
// VFDDisplay vfd(VFD_CS, VFD_CLK, VFD_DIN, 8);

extern "C" void app_main(void) {
  // Initialise the display (this also starts the SPI peripheral).
  vfd.init();
  // Clear the display
  vfd.clear();

  // Display a welcome message
  vfd.writeString(0, "VFD Library Test");
  vfd.setBrightness(120); // Set brightness to a medium level

  while (1) {
    // Example usage
    vfd.clear();
    vfd.writeChar(0, 'A');
    vTaskDelay(pdMS_TO_TICKS(2000));

    vfd.writeString(0, "Hello World!    ");
    vTaskDelay(pdMS_TO_TICKS(2000));

    vfd.writeString(0, "== Xuzhou Qin == ");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Test brightness control
    // for (int brightness = 255; brightness >= 50; brightness -= 50) {
    //   vfd.setBrightness(brightness);
    //   vfd.writeString(0, "Bright Test     ");
    //   vTaskDelay(pdMS_TO_TICKS(1000));
    // }

    // Reset to full brightness
    // vfd.setBrightness(255);
  }
}
