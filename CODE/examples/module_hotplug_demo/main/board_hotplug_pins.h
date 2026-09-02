#pragma once

#define HOTPLUG_ADC_GPIO            1
#define HOTPLUG_ADC_ATTEN           ADC_ATTEN_DB_12
#define HOTPLUG_ADC_UNIT            ADC_UNIT_1
#define HOTPLUG_ADC_CHANNEL         ADC_CHANNEL_0

#define HOTPLUG_I2C_PORT            0
#define HOTPLUG_CST836U_ADDR        0x15
#define HOTPLUG_SP0A39_ADDR         0x21

#define HOTPLUG_SLOT_NAME_C0        "C0 expansion board"
#define HOTPLUG_SLOT_NAME_D0        "D0 motor board"
#define HOTPLUG_SLOT_NAME_LCD       "LCD board"
#define HOTPLUG_SLOT_NAME_CAMERA    "Camera board"

#define HOTPLUG_LCD_CYCLE_MS        1000

#define HOTPLUG_ADC_SAMPLES         5
#define HOTPLUG_ADC_SAMPLE_INTERVAL_MS 50
#define HOTPLUG_ADC_UNSTABLE_SPREAD_RAW 200
#define HOTPLUG_STARTUP_DELAY_MS    10000
#define HOTPLUG_POLL_INTERVAL_MS    500
#define HOTPLUG_HEARTBEAT_MS        10000
