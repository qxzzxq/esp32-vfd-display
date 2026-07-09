#ifndef VFDDisplay_h
#define VFDDisplay_h

#include "driver/spi_master.h"

// C++ port of the Arduino VFDDisplay library for ESP-IDF.
//
// On the AVR original the clk/din arguments were ignored because the hardware
// SPI peripheral fixed those lines; on the ESP32 they are routed through the
// GPIO matrix, so clk (SCLK) and din (MOSI) are real, configurable pins here.
class VFDDisplay {
  public:
    VFDDisplay(int cs, int clk, int din, int numDigits = 16,
               spi_host_device_t host = SPI2_HOST);
    void init();
    void writeChar(unsigned char position, unsigned char character);
    void writeString(unsigned char position, const char* str);
    void setCustomChar(unsigned char slot, const unsigned char cols[5]);
    void setBrightness(unsigned char brightness);
    void clear();
    void show();

  private:
    spi_device_handle_t _spi;
    spi_host_device_t _host;
    int _cs_pin;
    int _clk_pin;
    int _din_pin;
    int _num_digits;
    void beginTransfer();
    void endTransfer();
    void spiWrite(unsigned char data);
    void sendCommand(unsigned char command);
    unsigned char digitsToByte(int digits);
};

#endif
