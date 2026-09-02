#include "touch_hal.h"

#include "board_pins.h"
#include "bus_i2c.h"
#include "io_expander.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch_hal";

#define CST836U_REG_TD_STATUS    0x02
#define CST836U_REG_P1_XH        0x03
#define CST836U_REG_P1_XL        0x04
#define CST836U_REG_P1_YH        0x05
#define CST836U_REG_P1_YL        0x06
#define CST836U_REG_FW_VER       0xA6
#define CST836U_REG_MODULE_ID    0xA8
#define CST836U_REG_CHIP_TYPE_H  0xAA
#define CST836U_REG_CHIP_TYPE_L  0xAB

static i2c_master_dev_handle_t s_dev;
static bool s_initialized;

static esp_err_t cst836u_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

static esp_err_t cst836u_read_regs(uint8_t start_reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &start_reg, 1, buf, len, 100);
}

esp_err_t touch_panel_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = bus_i2c_master_bus();
    ESP_RETURN_ON_FALSE(NULL != bus, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialized");

    esp_err_t ret = i2c_master_probe(bus, BOARD_LAIWFS300_CST836U_I2C_ADDR_7BIT, 100);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "CST836U not found at 0x%02X", BOARD_LAIWFS300_CST836U_I2C_ADDR_7BIT);
        return ESP_ERR_NOT_FOUND;
    }

    /* Reset touch via IOEX TP_RST (active low) */
    io_expander_set_pin_direction(BOARD_LAIWFS300_IOEX_TP_RST_PORT, BOARD_LAIWFS300_IOEX_TP_RST_PIN, true);
    io_expander_write_pin(BOARD_LAIWFS300_IOEX_TP_RST_PORT, BOARD_LAIWFS300_IOEX_TP_RST_PIN, false);
    vTaskDelay(pdMS_TO_TICKS(20));
    io_expander_write_pin(BOARD_LAIWFS300_IOEX_TP_RST_PORT, BOARD_LAIWFS300_IOEX_TP_RST_PIN, true);
    vTaskDelay(pdMS_TO_TICKS(200));

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_LAIWFS300_CST836U_I2C_ADDR_7BIT,
        .scl_speed_hz = BOARD_LAIWFS300_I2C_CLK_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "add I2C device");

    s_initialized = true;
    ESP_LOGI(TAG, "CST836U initialized at 0x%02X", BOARD_LAIWFS300_CST836U_I2C_ADDR_7BIT);
    return ESP_OK;
}

esp_err_t touch_panel_probe(void)
{
    i2c_master_bus_handle_t bus = bus_i2c_master_bus();
    if (NULL == bus) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_probe(bus, BOARD_LAIWFS300_CST836U_I2C_ADDR_7BIT, 100);
}

esp_err_t touch_panel_read_info(touch_panel_info_t *info)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(NULL != info, ESP_ERR_INVALID_ARG, TAG, "null info");

    uint8_t fw_ver = 0;
    uint8_t module_id = 0;
    uint8_t chip_type_h = 0;
    uint8_t chip_type_l = 0;

    ESP_RETURN_ON_ERROR(cst836u_read_reg(CST836U_REG_FW_VER, &fw_ver), TAG, "read fw_ver");
    ESP_RETURN_ON_ERROR(cst836u_read_reg(CST836U_REG_MODULE_ID, &module_id), TAG, "read module_id");
    ESP_RETURN_ON_ERROR(cst836u_read_reg(CST836U_REG_CHIP_TYPE_H, &chip_type_h), TAG, "read chip_type_h");
    ESP_RETURN_ON_ERROR(cst836u_read_reg(CST836U_REG_CHIP_TYPE_L, &chip_type_l), TAG, "read chip_type_l");

    info->chip_id = module_id;
    info->firmware_ver = fw_ver;
    info->lib_ver_h = chip_type_h;
    info->lib_ver_l = chip_type_l;

    ESP_LOGI(TAG, "CST836U info: module_id=0x%02X fw_ver=0x%02X chip_type=0x%02X%02X",
             module_id, fw_ver, chip_type_h, chip_type_l);
    return ESP_OK;
}

esp_err_t touch_panel_read_point(touch_panel_point_t *point, uint8_t *touch_count)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(NULL != point && NULL != touch_count, ESP_ERR_INVALID_ARG, TAG, "null args");

    uint8_t buf[7];
    ESP_RETURN_ON_ERROR(cst836u_read_regs(CST836U_REG_TD_STATUS, buf, sizeof(buf)), TAG, "read touch data");

    *touch_count = buf[0] & 0x0F;
    if (*touch_count > 0 && *touch_count <= 2) {
        uint8_t event = (buf[1] >> 6) & 0x03;
        uint16_t x = ((uint16_t)(buf[1] & 0x0F) << 8) | buf[2];
        uint16_t y = ((uint16_t)(buf[3] & 0x0F) << 8) | buf[4];
        point->x = x;
        point->y = y;
        point->weight = 0;
        point->event = event;
    } else {
        *touch_count = 0;
        point->x = 0;
        point->y = 0;
        point->weight = 0;
        point->event = 0;
    }
    return ESP_OK;
}
