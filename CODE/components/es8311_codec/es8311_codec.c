#include "es8311_codec.h"

#include "board_pins.h"
#include "bus_i2c.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "es8311";

#define ES8311_ADDR 0x18

static i2c_master_dev_handle_t s_dev;
static bool s_initialized;

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

esp_err_t es8311_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = bus_i2c_master_bus();
    ESP_RETURN_ON_FALSE(NULL != bus, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialized");

    esp_err_t ret = i2c_master_probe(bus, ES8311_ADDR, 100);
    if (ESP_OK != ret) {
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_ADDR,
        .scl_speed_hz = BOARD_LAIWFS300_I2C_CLK_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device");

    uint8_t id1 = 0, id2 = 0;
    read_reg(0xFD, &id1);
    read_reg(0xFE, &id2);
    ESP_LOGI(TAG, "ES8311 chip ID: 0x%02X 0x%02X", id1, id2);

    /* Reset all */
    ESP_RETURN_ON_ERROR(write_reg(0x00, 0x1F), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(write_reg(0x00, 0x80), TAG, "power on CSM");

    /* Clock: MCLK from MCLK pin, enable all clocks */
    ESP_RETURN_ON_ERROR(write_reg(0x01, 0x3F), TAG, "clk mgr1");
    /* MCLK divider = 1 (no pre-divide, no multiply) */
    ESP_RETURN_ON_ERROR(write_reg(0x02, 0x00), TAG, "clk mgr2");
    /* ADC OSR = 32 (for 16kHz with 256fs MCLK) */
    ESP_RETURN_ON_ERROR(write_reg(0x03, 0x10), TAG, "clk mgr3");
    /* DAC OSR = 32 */
    ESP_RETURN_ON_ERROR(write_reg(0x04, 0x10), TAG, "clk mgr4");
    /* ADC/DAC clock dividers */
    ESP_RETURN_ON_ERROR(write_reg(0x05, 0x00), TAG, "clk mgr5");
    /* BCLK divider: MCLK/4 for 16bit stereo 16kHz -> BCLK=512kHz */
    ESP_RETURN_ON_ERROR(write_reg(0x06, 0x03), TAG, "clk mgr6");
    /* LRCK divider high */
    ESP_RETURN_ON_ERROR(write_reg(0x07, 0x00), TAG, "clk mgr7");
    /* LRCK divider low: BCLK/32 per channel */
    ESP_RETURN_ON_ERROR(write_reg(0x08, 0xFF), TAG, "clk mgr8");

    /* SDP input: I2S, 16bit */
    ESP_RETURN_ON_ERROR(write_reg(0x09, 0x0C), TAG, "sdp in");
    /* SDP output: I2S, 16bit */
    ESP_RETURN_ON_ERROR(write_reg(0x0A, 0x0C), TAG, "sdp out");

    /* System: power up analog */
    ESP_RETURN_ON_ERROR(write_reg(0x0B, 0x00), TAG, "sys1");
    ESP_RETURN_ON_ERROR(write_reg(0x0C, 0x00), TAG, "sys2");
    /* Chip power on: ref up, vref enable */
    ESP_RETURN_ON_ERROR(write_reg(0x0D, 0x01), TAG, "sys3");
    ESP_RETURN_ON_ERROR(write_reg(0x0E, 0x02), TAG, "sys4");
    /* DAC settings */
    ESP_RETURN_ON_ERROR(write_reg(0x0F, 0x44), TAG, "sys5");
    ESP_RETURN_ON_ERROR(write_reg(0x10, 0x0C), TAG, "sys6");
    ESP_RETURN_ON_ERROR(write_reg(0x11, 0x00), TAG, "sys7");
    /* ADC settings */
    ESP_RETURN_ON_ERROR(write_reg(0x12, 0x00), TAG, "sys8");
    ESP_RETURN_ON_ERROR(write_reg(0x13, 0x10), TAG, "sys9");
    ESP_RETURN_ON_ERROR(write_reg(0x14, 0x10), TAG, "sys10");

    /* DAC volume: -10dB default */
    ESP_RETURN_ON_ERROR(write_reg(0x32, 0xBF), TAG, "dac vol");

    vTaskDelay(pdMS_TO_TICKS(50));

    s_initialized = true;
    ESP_LOGI(TAG, "ES8311 initialized (slave, I2S 16bit)");
    return ESP_OK;
}

esp_err_t es8311_start(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not init");
    /* Power on DAC, enable DAC output */
    ESP_RETURN_ON_ERROR(write_reg(0x00, 0x80), TAG, "start csm");
    ESP_RETURN_ON_ERROR(write_reg(0x12, 0x00), TAG, "start dac");
    ESP_LOGI(TAG, "DAC started");
    return ESP_OK;
}

esp_err_t es8311_stop(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_ERROR(write_reg(0x32, 0x00), TAG, "mute dac");
    ESP_LOGI(TAG, "DAC stopped");
    return ESP_OK;
}

esp_err_t es8311_set_volume(uint8_t volume)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_ERROR(write_reg(0x32, volume), TAG, "set volume");
    return ESP_OK;
}
