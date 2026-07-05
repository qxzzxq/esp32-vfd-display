#include "VFDDisplay.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_err.h"

// The VFD controller shifts data in LSB-first and latches each bit on the
// rising edge of the clock while it idles low, which is exactly SPI mode 0.
// 500 kHz is comfortably within the controller's timing; increase it if your
// wiring is short and clean, decrease it if you see garbled characters.
static const int VFD_SPI_CLOCK_HZ = 500000;

VFDDisplay::VFDDisplay(int cs, int clk, int din, int numDigits,
                       spi_host_device_t host) {
  _spi = nullptr;
  _host = host;
  _cs_pin = cs;
  _clk_pin = clk;
  _din_pin = din;
  _num_digits = numDigits;
}

void VFDDisplay::init() {
  // Chip select is driven manually so a whole command sequence can be held
  // with CS low, matching the beginTransfer()/endTransfer() pairs.
  gpio_config_t cs_cfg = {};
  cs_cfg.pin_bit_mask = 1ULL << _cs_pin;
  cs_cfg.mode = GPIO_MODE_OUTPUT;
  ESP_ERROR_CHECK(gpio_config(&cs_cfg));
  gpio_set_level((gpio_num_t)_cs_pin, 1); // deselect before the bus is configured

  spi_bus_config_t bus_cfg = {};
  bus_cfg.mosi_io_num = _din_pin;
  bus_cfg.miso_io_num = -1; // display is write-only
  bus_cfg.sclk_io_num = _clk_pin;
  bus_cfg.quadwp_io_num = -1;
  bus_cfg.quadhd_io_num = -1;
  ESP_ERROR_CHECK(spi_bus_initialize(_host, &bus_cfg, SPI_DMA_DISABLED));

  spi_device_interface_config_t dev_cfg = {};
  dev_cfg.clock_speed_hz = VFD_SPI_CLOCK_HZ;
  dev_cfg.mode = 0;                        // SPI mode 0
  dev_cfg.spics_io_num = -1;               // CS handled manually (see above)
  dev_cfg.queue_size = 1;
  dev_cfg.flags = SPI_DEVICE_BIT_LSBFIRST; // controller expects LSB first
  ESP_ERROR_CHECK(spi_bus_add_device(_host, &dev_cfg, &_spi));

  // Set number of digits
  beginTransfer();
  spiWrite(0xe0);
  esp_rom_delay_us(5);
  spiWrite(digitsToByte(_num_digits)); // Use converted byte value
  endTransfer();
  esp_rom_delay_us(5);

  // Set brightness to max by default
  setBrightness(0xff);
}

void VFDDisplay::beginTransfer() {
  gpio_set_level((gpio_num_t)_cs_pin, 0);
}

void VFDDisplay::endTransfer() {
  gpio_set_level((gpio_num_t)_cs_pin, 1);
}

void VFDDisplay::spiWrite(unsigned char data) {
  spi_transaction_t t = {};
  t.length = 8; // one byte, expressed in bits
  t.flags = SPI_TRANS_USE_TXDATA;
  t.tx_data[0] = data;
  ESP_ERROR_CHECK(spi_device_polling_transmit(_spi, &t));
}

void VFDDisplay::sendCommand(unsigned char command) {
  beginTransfer();
  spiWrite(command);
  endTransfer();
  esp_rom_delay_us(5);
}

void VFDDisplay::show() {
  beginTransfer();
  spiWrite(0xe8); // Display light ON/OFF -> Normal operation (LS=HS=0).
                  // Required: the controller powers up with all lights OFF.
  endTransfer();
}

void VFDDisplay::writeChar(unsigned char position, unsigned char character) {
  beginTransfer();
  spiWrite(0x20 + position);
  spiWrite(character);
  endTransfer();
  show();
}

void VFDDisplay::writeString(unsigned char position, const char* str) {
  beginTransfer();
  spiWrite(0x20 + position);
  while (*str) {
    spiWrite(*str++);
  }
  endTransfer();
  show();
}

void VFDDisplay::setBrightness(unsigned char brightness) {
  beginTransfer();
  spiWrite(0xe4);
  esp_rom_delay_us(5);
  spiWrite(brightness); // 240 dimming levels: 0=dimmest, >=240 all = max (240/255)
  endTransfer();
  esp_rom_delay_us(5);
}

void VFDDisplay::clear() {
  // Create a string with spaces equal to the number of digits
  char clearStr[17]; // up to 16 digits + null terminator
  int n = _num_digits;
  if (n < 0) n = 0;
  if (n > 16) n = 16;
  for (int i = 0; i < n; i++) {
    clearStr[i] = ' ';
  }
  clearStr[n] = '\0';
  writeString(0, clearStr);
}

unsigned char VFDDisplay::digitsToByte(int digits) {
  // Convert number of digits to the appropriate byte value
  // Common VFD controllers use: digits - 1 as the byte value
  // For example: 16 digits = 0x0F, 8 digits = 0x07, etc.
  if (digits <= 0 || digits > 16) {
    return 0x0F; // Default to 16 digits if invalid
  }
  return (unsigned char)(digits - 1);
}
