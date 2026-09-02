#include "board_laiwfs300.h"

#include "board_adc.h"
#include "board_module_map.h"
#include "board_pins.h"
#include "board_power.h"
#include "bus_i2c.h"
#include "bus_spi.h"
#include "capability_registry.h"
#include "io_expander.h"
#include "module_detect.h"

#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "board_laiwfs300";
static bool s_ioex_available;

bool board_laiwfs300_ioex_available(void)
{
    return s_ioex_available;
}

static esp_err_t register_default_detection_rules(void)
{
    const module_detect_adc_rule_t d0_rule = {
        .id = MODULE_ID_MOTOR_D0,
        .nominal_mv = BOARD_LAIWFS300_D0_ADC_NOMINAL_MV,
        .tolerance_mv = BOARD_LAIWFS300_D0_ADC_TOLERANCE_MV,
        .source = "C0/D0 ADC",
    };
    return module_detect_register_adc_rule(&d0_rule);
}

static esp_err_t register_default_capabilities(void)
{
    ESP_RETURN_ON_ERROR(capability_registry_set_state(CAPABILITY_AUDIO_IO, CAPABILITY_STATE_PENDING_DRIVER), TAG, "audio");
    ESP_RETURN_ON_ERROR(capability_registry_set_state(CAPABILITY_TRACK_MOTION, CAPABILITY_STATE_PENDING_DRIVER), TAG, "motion");
    ESP_RETURN_ON_ERROR(capability_registry_set_state(CAPABILITY_DISPLAY, CAPABILITY_STATE_PENDING_DRIVER), TAG, "display");
    ESP_RETURN_ON_ERROR(capability_registry_set_state(CAPABILITY_TOUCH, CAPABILITY_STATE_PENDING_DRIVER), TAG, "touch");
    ESP_RETURN_ON_ERROR(capability_registry_set_state(CAPABILITY_CAMERA, CAPABILITY_STATE_PENDING_DRIVER), TAG, "camera");
    ESP_RETURN_ON_ERROR(capability_registry_set_state(CAPABILITY_LTE, CAPABILITY_STATE_PENDING_DRIVER), TAG, "lte");
    ESP_RETURN_ON_ERROR(capability_registry_set_state(CAPABILITY_STORAGE, CAPABILITY_STATE_PENDING_DRIVER), TAG, "storage");
    return ESP_OK;
}

static void run_i2c_bus_scan(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "--- I2C bus scan (0x08-0x77) ---");
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (ESP_OK == i2c_master_probe(bus, addr, 50)) {
            ESP_LOGI(TAG, "  I2C device ACK at 0x%02X", addr);
            found++;
        }
    }
    ESP_LOGI(TAG, "--- I2C scan done: %d device(s) found ---", found);
}

static void probe_i2c_device(i2c_master_bus_handle_t bus, uint8_t addr, const char *name)
{
    if (ESP_OK == i2c_master_probe(bus, addr, 50)) {
        ESP_LOGI(TAG, "%s found at 0x%02X", name, addr);
    } else {
        ESP_LOGW(TAG, "%s NOT found at 0x%02X (NACK)", name, addr);
    }
}

static esp_err_t i2c_read_reg(i2c_master_bus_handle_t bus, uint8_t addr, uint8_t reg, uint8_t *val)
{
    i2c_master_dev_handle_t dev = NULL;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &cfg, &dev);
    if (ESP_OK != ret) { return ret; }
    ret = i2c_master_transmit_receive(dev, &reg, 1, val, 1, 100);
    i2c_master_bus_rm_device(dev);
    return ret;
}

static void verify_es8311(i2c_master_bus_handle_t bus)
{
    uint8_t id1 = 0, id2 = 0;
    if (ESP_OK == i2c_read_reg(bus, 0x18, 0xFD, &id1) &&
        ESP_OK == i2c_read_reg(bus, 0x18, 0xFE, &id2)) {
        ESP_LOGI(TAG, "ES8311 chip ID: 0x%02X 0x%02X", id1, id2);
    } else {
        ESP_LOGW(TAG, "ES8311 chip ID read failed");
    }
}

static void verify_es7210(i2c_master_bus_handle_t bus)
{
    uint8_t id = 0;
    if (ESP_OK == i2c_read_reg(bus, 0x40, 0x3D, &id)) {
        ESP_LOGI(TAG, "ES7210 chip ID: 0x%02X", id);
    } else {
        ESP_LOGW(TAG, "ES7210 chip ID read failed");
    }
}

static void verify_aip8563(i2c_master_bus_handle_t bus)
{
    uint8_t sec = 0, min = 0, hr = 0;
    if (ESP_OK == i2c_read_reg(bus, 0x51, 0x02, &sec) &&
        ESP_OK == i2c_read_reg(bus, 0x51, 0x03, &min) &&
        ESP_OK == i2c_read_reg(bus, 0x51, 0x04, &hr)) {
        ESP_LOGI(TAG, "AIP8563 RTC time: %02X:%02X:%02X (BCD raw)", hr & 0x3F, min & 0x7F, sec & 0x7F);
    } else {
        ESP_LOGW(TAG, "AIP8563 RTC time read failed");
    }
}

static void verify_bmi260(i2c_master_bus_handle_t bus)
{
    uint8_t chip_id = 0;
    if (ESP_OK == i2c_read_reg(bus, 0x68, 0x00, &chip_id)) {
        ESP_LOGI(TAG, "BMI260 chip ID: 0x%02X (expect 0x27)", chip_id);
    } else {
        ESP_LOGW(TAG, "BMI260 chip ID read failed");
    }
}

static void read_adc_channel(adc_oneshot_unit_handle_t adc, adc_channel_t ch, const char *name)
{
    int raw = 0;
    if (ESP_OK == adc_oneshot_read(adc, ch, &raw)) {
        int mv = raw * 3100 / 4095;
        ESP_LOGI(TAG, "ADC %s: raw=%d (~%d mV)", name, raw, mv);
    } else {
        ESP_LOGW(TAG, "ADC %s: read failed", name);
    }
}

esp_err_t board_laiwfs300_init(void)
{
    ESP_LOGI(TAG, "init board support package");

    const bus_i2c_config_t i2c_config = {
        .port = BOARD_LAIWFS300_I2C_PORT,
        .sda_gpio_num = BOARD_LAIWFS300_I2C_SDA_GPIO,
        .scl_gpio_num = BOARD_LAIWFS300_I2C_SCL_GPIO,
        .clk_hz = BOARD_LAIWFS300_I2C_CLK_HZ,
        .enable_internal_pullups = true,
    };

    ESP_LOGI(TAG, "init capability registry");
    ESP_RETURN_ON_ERROR(capability_registry_init(), TAG, "capability registry");
    ESP_LOGI(TAG, "init I2C bus: SDA=GPIO%d SCL=GPIO%d", BOARD_LAIWFS300_I2C_SDA_GPIO, BOARD_LAIWFS300_I2C_SCL_GPIO);
    ESP_RETURN_ON_ERROR(bus_i2c_init(&i2c_config), TAG, "i2c init");

    i2c_master_bus_handle_t bus = bus_i2c_master_bus();
    run_i2c_bus_scan(bus);

    probe_i2c_device(bus, 0x20, "TPT29555A (IOEX)");
    probe_i2c_device(bus, 0x18, "ES8311 (Codec)");
    probe_i2c_device(bus, 0x40, "ES7210 (ADC)");
    probe_i2c_device(bus, 0x51, "AIP8563 (RTC)");
    probe_i2c_device(bus, 0x68, "BMI260 (IMU @0x68)");
    probe_i2c_device(bus, 0x69, "BMI260 (IMU @0x69)");
    probe_i2c_device(bus, 0x21, "SP0A39 (Camera)");

    verify_es8311(bus);
    verify_es7210(bus);
    verify_aip8563(bus);
    verify_bmi260(bus);

    ESP_LOGI(TAG, "init SPI bus manager");
    const bus_spi_config_t spi_config = {
        .host = SPI2_HOST,
        .sclk_gpio_num = BOARD_LAIWFS300_GPIO_LCD_SPI_SCK,
        .mosi_gpio_num = BOARD_LAIWFS300_GPIO_LCD_SPI_MOSI,
        .miso_gpio_num = BOARD_LAIWFS300_GPIO_TF_SPI_MISO,
        .max_transfer_sz = BOARD_LAIWFS300_LCD_MAX_TRANSFER_SIZE,
        .dma_chan = SPI_DMA_CH_AUTO,
    };
    ESP_RETURN_ON_ERROR(bus_spi_init(&spi_config), TAG, "spi init");
    ESP_LOGI(TAG, "init ADC service");
    ESP_RETURN_ON_ERROR(board_adc_init(), TAG, "adc init");
    ESP_LOGI(TAG, "init board power service");
    ESP_RETURN_ON_ERROR(board_power_init(), TAG, "power init");

    adc_oneshot_unit_handle_t adc1 = board_adc_handle();
    if (NULL != adc1) {
        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        adc_oneshot_config_channel(adc1, ADC_CHANNEL_0, &chan_cfg);
        adc_oneshot_config_channel(adc1, ADC_CHANNEL_6, &chan_cfg);
        adc_oneshot_config_channel(adc1, ADC_CHANNEL_7, &chan_cfg);
        read_adc_channel(adc1, ADC_CHANNEL_0, "D0_DETECT (GPIO1)");
        read_adc_channel(adc1, ADC_CHANNEL_6, "BAT_ADC (GPIO7)");
        read_adc_channel(adc1, ADC_CHANNEL_7, "SW_ADC (GPIO8)");
    }

    const io_expander_config_t ioex_config = {
        .i2c_addr_7bit = BOARD_LAIWFS300_TPT29555A_I2C_ADDR_7BIT,
        .scl_speed_hz = BOARD_LAIWFS300_I2C_CLK_HZ,
    };
    ESP_LOGI(TAG, "init IO expander: TPT29555A addr=0x%02x", BOARD_LAIWFS300_TPT29555A_I2C_ADDR_7BIT);
    esp_err_t ioex_ret = io_expander_init(&ioex_config);
    if (ESP_OK == ioex_ret) {
        s_ioex_available = true;
        ESP_LOGI(TAG, "IO expander initialized OK");
    } else {
        s_ioex_available = false;
        ESP_LOGW(TAG, "IO expander init FAILED (ret=%s) - IOEX-dependent peripherals unavailable", esp_err_to_name(ioex_ret));
    }

    ESP_LOGI(TAG, "init module detect");
    ESP_RETURN_ON_ERROR(module_detect_init(), TAG, "module detect");
    ESP_LOGI(TAG, "register detection rules");
    ESP_RETURN_ON_ERROR(register_default_detection_rules(), TAG, "detection rules");
    ESP_LOGI(TAG, "register default capabilities");
    ESP_RETURN_ON_ERROR(register_default_capabilities(), TAG, "capabilities");

    ESP_LOGI(TAG, "board init complete (ioex=%s)", s_ioex_available ? "OK" : "FAILED");
    return ESP_OK;
}
