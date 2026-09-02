#include "es7210_adc.h"

#include "board_pins.h"
#include "bus_i2c.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "es7210";

#define ES7210_ADDR 0x40

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

esp_err_t es7210_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = bus_i2c_master_bus();
    ESP_RETURN_ON_FALSE(NULL != bus, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialized");

    esp_err_t ret = i2c_master_probe(bus, ES7210_ADDR, 100);
    if (ESP_OK != ret) {
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES7210_ADDR,
        .scl_speed_hz = BOARD_LAIWFS300_I2C_CLK_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device");

    uint8_t chip_id = 0;
    read_reg(0x3D, &chip_id);
    ESP_LOGI(TAG, "ES7210 chip ID: 0x%02X", chip_id);

    /* Software reset */
    ESP_RETURN_ON_ERROR(write_reg(0x00, 0xFF), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(write_reg(0x00, 0x32), TAG, "resume");
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Clock: slave mode, MCLK from MCLK pin, single speed */
    ESP_RETURN_ON_ERROR(write_reg(0x01, 0x20), TAG, "clk mode");
    /* LRCK divider = 256 (MCLK/LRCK ratio) */
    ESP_RETURN_ON_ERROR(write_reg(0x02, 0x01), TAG, "lrck_divh");
    ESP_RETURN_ON_ERROR(write_reg(0x03, 0x00), TAG, "lrck_divl");
    /* BCLK divider */
    ESP_RETURN_ON_ERROR(write_reg(0x04, 0x01), TAG, "bclk_divh");
    ESP_RETURN_ON_ERROR(write_reg(0x05, 0x00), TAG, "bclk_divl");

    /* ADC digital power enable: power on ADC1+ADC2 digital blocks */
    ESP_RETURN_ON_ERROR(write_reg(0x06, 0x00), TAG, "adc_digital_pwr");

    /* OSR configuration */
    ESP_RETURN_ON_ERROR(write_reg(0x07, 0x20), TAG, "osr");

    /* SDP format for ADC1/2: I2S, 16-bit */
    ESP_RETURN_ON_ERROR(write_reg(0x10, 0x60), TAG, "sdp1");
    /* SDP format for ADC3/4: I2S, 16-bit */
    ESP_RETURN_ON_ERROR(write_reg(0x11, 0x60), TAG, "sdp2");

    /* TDM enable for SDOUT1 (ADC1/2 on slot0/slot1) */
    ESP_RETURN_ON_ERROR(write_reg(0x12, 0x00), TAG, "tdm_ctrl");

    /* Power up ADC1 + ADC2 analog */
    ESP_RETURN_ON_ERROR(write_reg(0x40, 0x42), TAG, "adc_pwr");
    /* Enable MIC1 + MIC2 bias and preamp */
    ESP_RETURN_ON_ERROR(write_reg(0x41, 0x70), TAG, "mic1_bias");
    ESP_RETURN_ON_ERROR(write_reg(0x42, 0x70), TAG, "mic2_bias");
    /* MIC PGA gain: 30dB (reduced from 37.5dB to prevent clipping during TTS playback) */
    ESP_RETURN_ON_ERROR(write_reg(0x43, 0x3C), TAG, "mic1_gain");
    ESP_RETURN_ON_ERROR(write_reg(0x44, 0x3C), TAG, "mic2_gain");

    /* Analog reference power: enable VREF and VMID */
    ESP_RETURN_ON_ERROR(write_reg(0x4A, 0x0E), TAG, "vref_pwr");
    /* MIC PGA power enable */
    ESP_RETURN_ON_ERROR(write_reg(0x4B, 0x0E), TAG, "pga_pwr");

    vTaskDelay(pdMS_TO_TICKS(100));

    /* Verify readback of key registers */
    uint8_t reg06 = 0, reg10 = 0, reg40 = 0, reg43 = 0, reg44 = 0;
    read_reg(0x06, &reg06);
    read_reg(0x10, &reg10);
    read_reg(0x40, &reg40);
    read_reg(0x43, &reg43);
    read_reg(0x44, &reg44);
    ESP_LOGI(TAG, "ES7210 verify: reg06=0x%02X reg10=0x%02X reg40=0x%02X reg43=0x%02X reg44=0x%02X",
             reg06, reg10, reg40, reg43, reg44);

    s_initialized = true;
    ESP_LOGI(TAG, "ES7210 initialized (slave, I2S 16bit, 2ch, digital pwr ON, PGA=30dB)");
    return ESP_OK;
}

esp_err_t es7210_start(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not init");
    /* Enable clock and ADC digital */
    ESP_RETURN_ON_ERROR(write_reg(0x06, 0x00), TAG, "digital pwr on");
    ESP_RETURN_ON_ERROR(write_reg(0x00, 0x32), TAG, "start");
    return ESP_OK;
}

esp_err_t es7210_stop(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not init");
    /* Power down ADC digital */
    ESP_RETURN_ON_ERROR(write_reg(0x06, 0x07), TAG, "digital pwr off");
    ESP_RETURN_ON_ERROR(write_reg(0x00, 0x34), TAG, "stop");
    return ESP_OK;
}
