#include "bmi260_imu.h"
#include "bmi260_config.h"

#include "board_pins.h"
#include "bus_i2c.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "bmi260";

#define BMI260_ADDR     0x68
#define BMI260_CHIP_ID  0x27

#define REG_CHIP_ID     0x00
#define REG_DATA_8      0x0C
#define REG_DATA_14     0x12
#define REG_INTERNAL_STATUS 0x21
#define REG_ACC_CONF    0x40
#define REG_ACC_RANGE   0x41
#define REG_GYR_CONF    0x42
#define REG_GYR_RANGE   0x43
#define REG_INIT_CTRL   0x59
#define REG_INIT_ADDR_0 0x5B
#define REG_INIT_ADDR_1 0x5C
#define REG_INIT_DATA   0x5E
#define REG_PWR_CONF    0x7C
#define REG_PWR_CTRL    0x7D
#define REG_CMD         0x7E

#define BMI260_BURST_WRITE_LEN 256

static i2c_master_dev_handle_t s_dev;
static bool s_initialized;

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t read_regs(uint8_t start, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &start, 1, buf, len, 100);
}

static esp_err_t burst_write(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[BMI260_BURST_WRITE_LEN + 1];
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    return i2c_master_transmit(s_dev, buf, len + 1, 500);
}

static esp_err_t upload_config(void)
{
    ESP_RETURN_ON_ERROR(write_reg(REG_INIT_CTRL, 0x00), TAG, "init_ctrl=0");

    const size_t config_size = sizeof(bmi260_config_file);
    for (size_t i = 0; i < config_size; i += BMI260_BURST_WRITE_LEN) {
        uint16_t word_addr = (uint16_t)(i / 2);
        ESP_RETURN_ON_ERROR(write_reg(REG_INIT_ADDR_0, (uint8_t)(word_addr & 0x0F)), TAG, "addr0");
        ESP_RETURN_ON_ERROR(write_reg(REG_INIT_ADDR_1, (uint8_t)(word_addr >> 4)), TAG, "addr1");

        size_t chunk = config_size - i;
        if (chunk > BMI260_BURST_WRITE_LEN) {
            chunk = BMI260_BURST_WRITE_LEN;
        }
        ESP_RETURN_ON_ERROR(burst_write(REG_INIT_DATA, &bmi260_config_file[i], chunk),
                            TAG, "burst @%u", (unsigned)i);
    }

    ESP_RETURN_ON_ERROR(write_reg(REG_INIT_CTRL, 0x01), TAG, "init_ctrl=1");
    vTaskDelay(pdMS_TO_TICKS(150));

    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(read_regs(REG_INTERNAL_STATUS, &status, 1), TAG, "read status");
    ESP_RETURN_ON_FALSE((status & 0x0F) == 0x01, ESP_ERR_INVALID_RESPONSE, TAG,
                        "config load failed: internal_status=0x%02X", status);

    ESP_LOGI(TAG, "config uploaded (%u bytes), internal_status=0x%02X", (unsigned)config_size, status);
    return ESP_OK;
}

esp_err_t bmi260_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = bus_i2c_master_bus();
    ESP_RETURN_ON_FALSE(NULL != bus, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialized");

    esp_err_t ret = i2c_master_probe(bus, BMI260_ADDR, 100);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "BMI260 not found at 0x%02X", BMI260_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BMI260_ADDR,
        .scl_speed_hz = BOARD_LAIWFS300_I2C_CLK_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device");

    uint8_t chip_id = 0;
    ESP_RETURN_ON_ERROR(read_regs(REG_CHIP_ID, &chip_id, 1), TAG, "read chip_id");
    ESP_RETURN_ON_FALSE(chip_id == BMI260_CHIP_ID, ESP_ERR_NOT_FOUND, TAG,
                        "unexpected chip_id 0x%02X (expect 0x%02X)", chip_id, BMI260_CHIP_ID);

    ESP_RETURN_ON_ERROR(write_reg(REG_CMD, 0xB6), TAG, "soft reset");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_RETURN_ON_ERROR(write_reg(REG_PWR_CONF, 0x00), TAG, "pwr_conf");
    vTaskDelay(pdMS_TO_TICKS(1));

    ESP_RETURN_ON_ERROR(upload_config(), TAG, "upload config");

    ESP_RETURN_ON_ERROR(write_reg(REG_PWR_CTRL, 0x0E), TAG, "pwr_ctrl");
    vTaskDelay(pdMS_TO_TICKS(1));

    ESP_RETURN_ON_ERROR(write_reg(REG_ACC_CONF, 0xA8), TAG, "acc_conf");
    ESP_RETURN_ON_ERROR(write_reg(REG_ACC_RANGE, 0x01), TAG, "acc_range");
    ESP_RETURN_ON_ERROR(write_reg(REG_GYR_CONF, 0xA9), TAG, "gyr_conf");
    ESP_RETURN_ON_ERROR(write_reg(REG_GYR_RANGE, 0x00), TAG, "gyr_range");

    vTaskDelay(pdMS_TO_TICKS(50));

    s_initialized = true;
    ESP_LOGI(TAG, "BMI260 initialized (chip_id=0x%02X)", chip_id);
    return ESP_OK;
}

esp_err_t bmi260_read_accel(bmi260_raw_data_t *data)
{
    ESP_RETURN_ON_FALSE(s_initialized && data, ESP_ERR_INVALID_STATE, TAG, "not ready");

    uint8_t raw[6];
    ESP_RETURN_ON_ERROR(read_regs(REG_DATA_8, raw, sizeof(raw)), TAG, "read accel");
    data->x = (int16_t)((uint16_t)raw[1] << 8 | raw[0]);
    data->y = (int16_t)((uint16_t)raw[3] << 8 | raw[2]);
    data->z = (int16_t)((uint16_t)raw[5] << 8 | raw[4]);
    return ESP_OK;
}

esp_err_t bmi260_read_gyro(bmi260_raw_data_t *data)
{
    ESP_RETURN_ON_FALSE(s_initialized && data, ESP_ERR_INVALID_STATE, TAG, "not ready");

    uint8_t raw[6];
    ESP_RETURN_ON_ERROR(read_regs(REG_DATA_14, raw, sizeof(raw)), TAG, "read gyro");
    data->x = (int16_t)((uint16_t)raw[1] << 8 | raw[0]);
    data->y = (int16_t)((uint16_t)raw[3] << 8 | raw[2]);
    data->z = (int16_t)((uint16_t)raw[5] << 8 | raw[4]);
    return ESP_OK;
}
