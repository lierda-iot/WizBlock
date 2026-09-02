#include "aip8563_rtc.h"

#include "board_pins.h"
#include "bus_i2c.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "aip8563";

#define AIP8563_ADDR 0x51

#define REG_CTRL1   0x00
#define REG_CTRL2   0x01
#define REG_SEC     0x02
#define REG_MIN     0x03
#define REG_HOUR    0x04
#define REG_DAY     0x05
#define REG_WDAY    0x06
#define REG_MON     0x07
#define REG_YEAR    0x08

static i2c_master_dev_handle_t s_dev;
static bool s_initialized;

static uint8_t dec_to_bcd(uint8_t val) { return ((val / 10) << 4) | (val % 10); }
static uint8_t bcd_to_dec(uint8_t val) { return ((val >> 4) * 10) + (val & 0x0F); }

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t read_regs(uint8_t start, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &start, 1, buf, len, 100);
}

esp_err_t aip8563_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = bus_i2c_master_bus();
    ESP_RETURN_ON_FALSE(NULL != bus, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialized");

    esp_err_t ret = i2c_master_probe(bus, AIP8563_ADDR, 100);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "AIP8563 not found at 0x%02X", AIP8563_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AIP8563_ADDR,
        .scl_speed_hz = BOARD_LAIWFS300_I2C_CLK_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device");

    ESP_RETURN_ON_ERROR(write_reg(REG_CTRL1, 0x00), TAG, "ctrl1");
    ESP_RETURN_ON_ERROR(write_reg(REG_CTRL2, 0x00), TAG, "ctrl2");

    s_initialized = true;
    ESP_LOGI(TAG, "AIP8563 RTC initialized");
    return ESP_OK;
}

esp_err_t aip8563_set_time(const aip8563_time_t *time)
{
    ESP_RETURN_ON_FALSE(s_initialized && time, ESP_ERR_INVALID_STATE, TAG, "not ready");

    uint8_t buf[8];
    buf[0] = REG_SEC;
    buf[1] = dec_to_bcd(time->seconds) & 0x7F;
    buf[2] = dec_to_bcd(time->minutes) & 0x7F;
    buf[3] = dec_to_bcd(time->hours) & 0x3F;
    buf[4] = dec_to_bcd(time->day) & 0x3F;
    buf[5] = time->weekday & 0x07;
    buf[6] = dec_to_bcd(time->month) & 0x1F;
    buf[7] = dec_to_bcd(time->year);

    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, buf, sizeof(buf), 100), TAG, "set time");
    ESP_LOGI(TAG, "set time: %02u:%02u:%02u %04u-%02u-%02u",
             time->hours, time->minutes, time->seconds,
             2000 + time->year, time->month, time->day);
    return ESP_OK;
}

esp_err_t aip8563_get_time(aip8563_time_t *time)
{
    ESP_RETURN_ON_FALSE(s_initialized && time, ESP_ERR_INVALID_STATE, TAG, "not ready");

    uint8_t raw[7];
    ESP_RETURN_ON_ERROR(read_regs(REG_SEC, raw, sizeof(raw)), TAG, "read time");

    time->seconds = bcd_to_dec(raw[0] & 0x7F);
    time->minutes = bcd_to_dec(raw[1] & 0x7F);
    time->hours   = bcd_to_dec(raw[2] & 0x3F);
    time->day     = bcd_to_dec(raw[3] & 0x3F);
    time->weekday = raw[4] & 0x07;
    time->month   = bcd_to_dec(raw[5] & 0x1F);
    time->year    = bcd_to_dec(raw[6]);
    return ESP_OK;
}

bool aip8563_power_lost(void)
{
    if (!s_initialized) {
        return true;
    }
    uint8_t val = 0;
    if (ESP_OK != read_regs(REG_SEC, &val, 1)) {
        return true;
    }
    return (val & 0x80) != 0;
}
