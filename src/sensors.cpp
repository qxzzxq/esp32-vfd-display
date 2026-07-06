#include "sensors.h"

#include <math.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define I2C_SDA GPIO_NUM_4
#define I2C_SCL GPIO_NUM_5
#define I2C_TIMEOUT_MS 100

#define AHT20_ADDR       0x38
#define BMP280_ADDR      0x76
#define BMP280_ADDR_ALT  0x77

static const char* TAG = "sensors";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_aht;
static i2c_master_dev_handle_t s_bmp;
static bool s_aht_inited;
static bool s_has_bmp;

// BMP280 calibration words (register map 0x88..0x9F)
static uint16_t dig_T1, dig_P1;
static int16_t dig_T2, dig_T3, dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;

static SemaphoreHandle_t s_mutex;
static bool s_valid;
static float s_tC, s_rh, s_hPa = NAN;

// ---- AHT20 ----

static uint8_t crc8(const uint8_t* d, int n) {
    uint8_t crc = 0xFF;  // poly 0x31, init 0xFF (datasheet)
    for (int i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}

static bool aht20_init() {
    uint8_t status;
    if (i2c_master_receive(s_aht, &status, 1, I2C_TIMEOUT_MS) != ESP_OK) return false;
    if (!(status & 0x08)) {  // calibration bit clear -> send init
        const uint8_t cmd[3] = {0xBE, 0x08, 0x00};
        if (i2c_master_transmit(s_aht, cmd, 3, I2C_TIMEOUT_MS) != ESP_OK) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

static bool aht20_read(float* tC, float* rh) {
    const uint8_t cmd[3] = {0xAC, 0x33, 0x00};
    if (i2c_master_transmit(s_aht, cmd, 3, I2C_TIMEOUT_MS) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(80));  // measurement time
    uint8_t d[7];
    for (int tries = 0;; tries++) {
        if (i2c_master_receive(s_aht, d, 7, I2C_TIMEOUT_MS) != ESP_OK) return false;
        if (!(d[0] & 0x80)) break;  // busy flag cleared
        if (tries >= 3) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (crc8(d, 6) != d[6]) return false;
    uint32_t raw_rh = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    uint32_t raw_t = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
    *rh = raw_rh * 100.0f / 1048576.0f;
    *tC = raw_t * 200.0f / 1048576.0f - 50.0f;
    return true;
}

// ---- BMP280 ----

static bool bmp_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_bmp, buf, 2, I2C_TIMEOUT_MS) == ESP_OK;
}

static bool bmp_read_regs(uint8_t reg, uint8_t* dst, size_t n) {
    return i2c_master_transmit_receive(s_bmp, &reg, 1, dst, n, I2C_TIMEOUT_MS) == ESP_OK;
}

static bool bmp280_init() {
    uint8_t id;
    if (!bmp_read_regs(0xD0, &id, 1)) return false;
    if (id != 0x58 && id != 0x60) return false;  // BMP280 / BME280
    uint8_t c[24];
    if (!bmp_read_regs(0x88, c, 24)) return false;
    dig_T1 = (uint16_t)(c[1] << 8 | c[0]);
    dig_T2 = (int16_t)(c[3] << 8 | c[2]);
    dig_T3 = (int16_t)(c[5] << 8 | c[4]);
    dig_P1 = (uint16_t)(c[7] << 8 | c[6]);
    dig_P2 = (int16_t)(c[9] << 8 | c[8]);
    dig_P3 = (int16_t)(c[11] << 8 | c[10]);
    dig_P4 = (int16_t)(c[13] << 8 | c[12]);
    dig_P5 = (int16_t)(c[15] << 8 | c[14]);
    dig_P6 = (int16_t)(c[17] << 8 | c[16]);
    dig_P7 = (int16_t)(c[19] << 8 | c[18]);
    dig_P8 = (int16_t)(c[21] << 8 | c[20]);
    dig_P9 = (int16_t)(c[23] << 8 | c[22]);
    // config: standby 500 ms, IIR filter x4; ctrl_meas: T x2, P x16, normal mode
    return bmp_write_reg(0xF5, 0x88) && bmp_write_reg(0xF4, 0x57);
}

static bool bmp280_read(float* hPa) {
    uint8_t d[6];
    if (!bmp_read_regs(0xF7, d, 6)) return false;
    int32_t adc_P = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    int32_t adc_T = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    // Bosch datasheet integer compensation (temp only feeds t_fine for pressure)
    int32_t v1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    int32_t v2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) *
                    ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
    int32_t t_fine = v1 + v2;
    int64_t p1 = (int64_t)t_fine - 128000;
    int64_t p2 = p1 * p1 * (int64_t)dig_P6;
    p2 += (p1 * (int64_t)dig_P5) << 17;
    p2 += ((int64_t)dig_P4) << 35;
    p1 = ((p1 * p1 * (int64_t)dig_P3) >> 8) + ((p1 * (int64_t)dig_P2) << 12);
    p1 = ((((int64_t)1 << 47) + p1) * (int64_t)dig_P1) >> 33;
    if (p1 == 0) return false;
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - p2) * 3125) / p1;
    p1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    p2 = (((int64_t)dig_P8) * p) >> 19;
    p = ((p + p1 + p2) >> 8) + (((int64_t)dig_P7) << 4);
    *hPa = (float)p / 256.0f / 100.0f;  // Q24.8 Pa -> hPa
    return true;
}

// ---- public API ----

void sensors_init() {
    s_mutex = xSemaphoreCreateMutex();

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = -1;  // auto-select
    bus_cfg.sda_io_num = I2C_SDA;
    bus_cfg.scl_io_num = I2C_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;  // backstop; module has its own
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_bus));

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = AHT20_ADDR;
    dev_cfg.scl_speed_hz = 100000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_aht));
    s_aht_inited = aht20_init();
    if (!s_aht_inited) ESP_LOGW(TAG, "AHT20 not responding at 0x38");

    uint16_t bmp_addr = 0;
    if (i2c_master_probe(s_bus, BMP280_ADDR, I2C_TIMEOUT_MS) == ESP_OK)
        bmp_addr = BMP280_ADDR;
    else if (i2c_master_probe(s_bus, BMP280_ADDR_ALT, I2C_TIMEOUT_MS) == ESP_OK)
        bmp_addr = BMP280_ADDR_ALT;
    if (bmp_addr) {
        dev_cfg.device_address = bmp_addr;
        ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_bmp));
        s_has_bmp = bmp280_init();
        ESP_LOGI(TAG, "BMP280 at 0x%02x: %s", bmp_addr, s_has_bmp ? "ok" : "init failed");
    } else {
        ESP_LOGW(TAG, "BMP280 not found at 0x76/0x77 - pressure page disabled");
    }
}

bool sensors_read() {
    if (!s_aht_inited) s_aht_inited = aht20_init();  // recover from unplug/boot race
    float t = 0, rh = 0, hPa = NAN;
    bool ok = s_aht_inited && aht20_read(&t, &rh);
    if (!ok) s_aht_inited = false;
    if (s_has_bmp) bmp280_read(&hPa);  // stays NAN on failure

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_valid = ok;
    if (ok) {
        s_tC = t;
        s_rh = rh;
    }
    s_hPa = hPa;
    xSemaphoreGive(s_mutex);

    if (!ok) ESP_LOGW(TAG, "AHT20 read failed");
    return ok;
}

bool sensors_get(float* tC, float* rh, float* hPa) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *tC = s_tC;
    *rh = s_rh;
    *hPa = s_hPa;
    bool valid = s_valid;
    xSemaphoreGive(s_mutex);
    return valid;
}

bool sensors_has_pressure() {
    return s_has_bmp;
}
