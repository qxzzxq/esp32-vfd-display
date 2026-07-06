#ifndef sensors_h
#define sensors_h

// AHT20 (temp/humidity @0x38) + BMP280 (pressure @0x76/0x77) on I2C
// (SDA=GPIO4, SCL=GPIO5). sensors_read() takes a reading from a polling
// task; the UI consumes the latest values via sensors_get().
void sensors_init();

// Blocking ~100 ms; call from the polling task only.
bool sensors_read();

// false until the first successful read, or when the last read failed
// (sensor error). *hPa is NAN when the BMP280 reading is unavailable.
bool sensors_get(float* tC, float* rh, float* hPa);

// BMP280 detected at boot; the PRESSURE page is omitted when false.
bool sensors_has_pressure();

#endif
