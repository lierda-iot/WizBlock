#include "holocubic_input.h"
#include "holocubic_frame_format.h"
#include "holocubic_model.h"
#include "holocubic_network_policy.h"
#include "holocubic_periodic.h"
#include "holocubic_render_policy.h"
#include "holocubic_spi_policy.h"
#include "holocubic_startup_policy.h"
#include "holocubic_time.h"
#include "holocubic_ui_clock.h"
#include "holocubic_ui_state.h"
#include "holocubic_visual_ui.h"
#include "holocubic_weather.h"
#include "holocubic_wifi_buffer_policy.h"
#include "holocubic_wifi.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *VALID_WEATHER_JSON =
    "{\"current\":{\"time\":\"2026-08-13T12:30\","
    "\"temperature_2m\":35.5,\"relative_humidity_2m\":62,"
    "\"weather_code\":2},\"daily\":{\"temperature_2m_max\":[38.0],"
    "\"temperature_2m_min\":[28.0]}}";

static void write_le16(uint8_t *destination, uint16_t value)
{
    destination[0U] = (uint8_t)(value & 0xFFU);
    destination[1U] = (uint8_t)(value >> 8U);
}

static void write_le32(uint8_t *destination, uint32_t value)
{
    destination[0U] = (uint8_t)(value & 0xFFU);
    destination[1U] = (uint8_t)((value >> 8U) & 0xFFU);
    destination[2U] = (uint8_t)((value >> 16U) & 0xFFU);
    destination[3U] = (uint8_t)(value >> 24U);
}

static void make_frame_image_header(uint8_t *header, uint32_t crc32)
{
    assert(NULL != header);
    memset(header, 0, HOLO_FRAME_IMAGE_HEADER_BYTES);
    memcpy(header, HOLO_FRAME_IMAGE_MAGIC, HOLO_FRAME_IMAGE_MAGIC_BYTES);
    write_le16(header + 4U, HOLO_FRAME_IMAGE_VERSION);
    write_le16(header + 6U, HOLO_FRAME_IMAGE_HEADER_BYTES);
    write_le16(header + 8U, HOLO_FRAME_WIDTH);
    write_le16(header + 10U, HOLO_FRAME_HEIGHT);
    write_le16(header + 12U, HOLO_FRAME_PIXEL_FORMAT_RGB565LE);
    write_le16(header + 14U, HOLO_FRAME_IMAGE_FRAME_COUNT);
    write_le32(header + 16U, HOLO_FRAME_PERIOD_MS);
    write_le32(header + 20U, HOLO_FRAME_IMAGE_PAYLOAD_BYTES);
    write_le32(header + 24U, crc32);
    write_le32(header + 28U, 0U);
}

static void test_layout(void)
{
    holocubic_rect_t rect = holocubic_layout(320U, 240U);
    assert(40U == rect.x);
    assert(0U == rect.y);
    assert(240U == rect.width);
    assert(240U == rect.height);
    assert(0U == holocubic_scale_coordinate(0U, 240U, 240U));
    assert(120U == holocubic_scale_coordinate(120U, 240U, 240U));
    assert(239U == holocubic_scale_coordinate(239U, 240U, 240U));

    rect = holocubic_layout(321U, 241U);
    assert(40U == rect.x);
    assert(0U == rect.y);
    assert(240U == rect.width && 240U == rect.height);
    rect = holocubic_layout(200U, 100U);
    assert(0U == rect.x && 0U == rect.y);
    assert(200U == rect.width && 100U == rect.height);
}

static void test_frame_format(void)
{
    static uint16_t frame[HOLO_FRAME_PIXELS];
    static uint16_t canvas[HOLO_LOGICAL_WIDTH * HOLO_LOGICAL_HEIGHT];
    uint8_t header[HOLO_FRAME_IMAGE_HEADER_BYTES] = {0};
    holocubic_frame_image_info_t image_info = {0};
    size_t cache_bytes = 0U;
    size_t frame_offset = 0U;

    assert(240U == HOLO_FRAME_WIDTH);
    assert(240U == HOLO_FRAME_HEIGHT);
    assert(115200U == HOLO_FRAME_BYTES);
    assert(32U == HOLO_FRAME_IMAGE_HEADER_BYTES);
    assert(49U == HOLO_FRAME_IMAGE_FRAME_COUNT);
    assert(100U == HOLO_FRAME_PERIOD_MS);
    assert(5644800U == HOLO_FRAME_IMAGE_PAYLOAD_BYTES);
    assert(5644832U == HOLO_FRAME_IMAGE_BYTES);
    assert(0xCBF43926U == holocubic_frame_crc32(
        (const uint8_t *)"123456789", 9U));
    assert(0U == holocubic_frame_crc32(NULL, 0U));
    assert(0U == holocubic_frame_crc32(NULL, 1U));
    make_frame_image_header(header, 0x12345678U);
    assert(holocubic_frame_image_parse(header, sizeof(header), 0x570000U,
                                        &image_info));
    assert(HOLO_FRAME_IMAGE_FRAME_COUNT == image_info.frame_count);
    assert(HOLO_FRAME_PERIOD_MS == image_info.frame_period_ms);
    assert(HOLO_FRAME_IMAGE_PAYLOAD_BYTES == image_info.payload_bytes);
    assert(0x12345678U == image_info.payload_crc32);
    assert(HOLO_FRAME_IMAGE_HEADER_BYTES == image_info.payload_offset);
    assert(holocubic_frame_image_frame_offset(&image_info, 0U,
                                               &frame_offset));
    assert(HOLO_FRAME_IMAGE_HEADER_BYTES == frame_offset);
    assert(holocubic_frame_image_frame_offset(&image_info, 48U,
                                               &frame_offset));
    assert(HOLO_FRAME_IMAGE_HEADER_BYTES + (48U * HOLO_FRAME_BYTES) ==
           frame_offset);
    assert(!holocubic_frame_image_frame_offset(&image_info, 49U,
                                                &frame_offset));
    assert(!holocubic_frame_image_frame_offset(&image_info, 0U, NULL));
    assert(!holocubic_frame_image_parse(NULL, sizeof(header), 0x570000U,
                                        &image_info));
    assert(!holocubic_frame_image_parse(header, sizeof(header) - 1U,
                                        0x570000U, &image_info));
    assert(!holocubic_frame_image_parse(header, sizeof(header),
                                        HOLO_FRAME_IMAGE_BYTES - 1U,
                                        &image_info));
    header[0U] ^= 0x01U;
    assert(!holocubic_frame_image_parse(header, sizeof(header), 0x570000U,
                                        &image_info));
    header[0U] ^= 0x01U;
    write_le16(header + 14U, 48U);
    assert(!holocubic_frame_image_parse(header, sizeof(header), 0x570000U,
                                        &image_info));
    write_le16(header + 14U, HOLO_FRAME_IMAGE_FRAME_COUNT);
    write_le32(header + 28U, 1U);
    assert(!holocubic_frame_image_parse(header, sizeof(header), 0x570000U,
                                        &image_info));
    make_frame_image_header(header, 0x12345678U);
    assert(0x1F00U == holocubic_rgb565_to_display(0xF800U));
    assert(0xE007U == holocubic_rgb565_to_display(0x07E0U));
    assert(0x00F8U == holocubic_rgb565_to_display(0x001FU));
    assert(0x0000U == holocubic_rgb565_to_display(0x0000U));
    assert(0xFFFFU == holocubic_rgb565_to_display(0xFFFFU));
    assert(holocubic_frame_cache_plan(49U, HOLO_FRAME_IMAGE_PAYLOAD_BYTES,
                                      &cache_bytes));
    assert(5644800U == cache_bytes);
    assert(!holocubic_frame_cache_plan(50U, HOLO_FRAME_IMAGE_PAYLOAD_BYTES,
                                       &cache_bytes));
    assert(!holocubic_frame_cache_plan(0U, HOLO_FRAME_IMAGE_PAYLOAD_BYTES,
                                       &cache_bytes));
    assert(!holocubic_frame_cache_plan(49U, HOLO_FRAME_IMAGE_PAYLOAD_BYTES,
                                       NULL));
    assert(1U == holocubic_frame_next_index(0U, 49U));
    assert(0U == holocubic_frame_next_index(48U, 49U));
    assert(0U == holocubic_frame_next_index(49U, 49U));
    assert(0U == holocubic_frame_next_index(0U, 0U));
    assert(1U == holocubic_frame_revision_next(0U));
    assert(2U == holocubic_frame_revision_next(1U));
    assert(1U == holocubic_frame_revision_next(UINT32_MAX));
    frame[0U] = 0x1111U;
    frame[(120U * HOLO_FRAME_WIDTH) + 120U] = 0x2222U;
    frame[HOLO_FRAME_PIXELS - 1U] = 0x3333U;
    assert(holocubic_frame_scale_to_canvas(
        frame, HOLO_FRAME_PIXELS, canvas,
        HOLO_LOGICAL_WIDTH * HOLO_LOGICAL_HEIGHT));
    assert(0x1111U == canvas[0U]);
    assert(0x2222U == canvas[(120U * HOLO_LOGICAL_WIDTH) + 120U]);
    assert(0x3333U == canvas[(HOLO_LOGICAL_HEIGHT - 1U) * HOLO_LOGICAL_WIDTH +
                             HOLO_LOGICAL_WIDTH - 1U]);
    assert(!holocubic_frame_scale_to_canvas(
        NULL, HOLO_FRAME_PIXELS, canvas,
        HOLO_LOGICAL_WIDTH * HOLO_LOGICAL_HEIGHT));
    assert(!holocubic_frame_scale_to_canvas(
        frame, HOLO_FRAME_PIXELS - 1U, canvas,
        HOLO_LOGICAL_WIDTH * HOLO_LOGICAL_HEIGHT));
    assert(!holocubic_frame_scale_to_canvas(
        frame, HOLO_FRAME_PIXELS, canvas,
        (HOLO_LOGICAL_WIDTH * HOLO_LOGICAL_HEIGHT) - 1U));
}

static void test_periodic(void)
{
    holocubic_periodic_t periodic = {0};

    assert(!holocubic_periodic_init(NULL, 1000U, 100U));
    assert(!holocubic_periodic_init(&periodic, 1000U, 0U));
    assert(holocubic_periodic_init(&periodic, 1000U, 100U));
    assert(100U == holocubic_periodic_next_delay(&periodic, 1000U));
    assert(50U == holocubic_periodic_next_delay(&periodic, 1150U));
    assert(0U == holocubic_periodic_next_delay(&periodic, 1300U));
    assert(50U == holocubic_periodic_next_delay(&periodic, 1350U));
    assert(99U == holocubic_periodic_next_delay(&periodic, 1701U));
    assert(0U == holocubic_periodic_next_delay(NULL, 1800U));
}

static void test_spi_policy(void)
{
    holocubic_spi_policy_t policy = {0};

    holocubic_spi_policy_init(&policy);
    assert(HOLO_SPI_OWNER_NONE == policy.owner);
    assert(holocubic_spi_policy_try_acquire(&policy, HOLO_SPI_OWNER_DISPLAY));
    assert(!holocubic_spi_policy_try_acquire(&policy, HOLO_SPI_OWNER_STORAGE));
    assert(!holocubic_spi_policy_release(&policy, HOLO_SPI_OWNER_STORAGE));
    assert(HOLO_SPI_OWNER_DISPLAY == policy.owner);
    assert(holocubic_spi_policy_release(&policy, HOLO_SPI_OWNER_DISPLAY));
    assert(holocubic_spi_policy_try_acquire(&policy, HOLO_SPI_OWNER_STORAGE));
    assert(!holocubic_spi_policy_try_acquire(&policy, HOLO_SPI_OWNER_DISPLAY));
    assert(holocubic_spi_policy_release(&policy, HOLO_SPI_OWNER_STORAGE));
    assert(!holocubic_spi_policy_try_acquire(&policy, HOLO_SPI_OWNER_NONE));
    assert(!holocubic_spi_policy_try_acquire(NULL, HOLO_SPI_OWNER_DISPLAY));
    assert(!holocubic_spi_policy_release(NULL, HOLO_SPI_OWNER_DISPLAY));
}

static void test_startup_plan(void)
{
    holocubic_startup_step_t steps[HOLO_STARTUP_STEP_COUNT] = {0};

    assert(!holocubic_startup_plan(NULL, HOLO_STARTUP_STEP_COUNT));
    assert(!holocubic_startup_plan(steps, HOLO_STARTUP_STEP_COUNT - 1U));
    assert(holocubic_startup_plan(steps, HOLO_STARTUP_STEP_COUNT));
    assert(HOLO_STARTUP_STEP_NETWORK == steps[0U]);
    assert(HOLO_STARTUP_STEP_TOUCH == steps[1U]);
    assert(HOLO_STARTUP_STEP_SPECTRUM == steps[2U]);
    assert(HOLO_STARTUP_STEP_RENDER == steps[3U]);
    assert(HOLO_STARTUP_STEP_FRAMES == steps[4U]);
    assert(HOLO_TASK_STACK_EXTERNAL ==
           holocubic_startup_task_stack(HOLO_STARTUP_STEP_SPECTRUM));
    assert(HOLO_TASK_STACK_EXTERNAL ==
           holocubic_startup_task_stack(HOLO_STARTUP_STEP_RENDER));
    assert(HOLO_TASK_STACK_EXTERNAL ==
           holocubic_runtime_task_stack(HOLO_RUNTIME_TASK_FRAMES));
    assert(HOLO_TASK_STACK_INTERNAL ==
           holocubic_runtime_task_stack(HOLO_RUNTIME_TASK_NETWORK));
    assert(HOLO_TASK_STACK_EXTERNAL ==
           holocubic_runtime_task_stack(HOLO_RUNTIME_TASK_INPUT));
    assert(HOLO_TASK_STACK_EXTERNAL ==
           holocubic_runtime_task_stack(HOLO_RUNTIME_TASK_TIME));
}

static void test_model(void)
{
    holocubic_model_t model = {0};

    holocubic_model_init(&model);
    assert(HOLO_PAGE_ANIMATION == model.page);
    assert(holocubic_model_dispatch(&model, HOLO_COMMAND_NEXT, 1000U));
    assert(HOLO_PAGE_WEATHER == model.page);
    assert(!holocubic_model_dispatch(&model, HOLO_COMMAND_NEXT, 1299U));
    assert(HOLO_PAGE_WEATHER == model.page);
    assert(holocubic_model_dispatch(&model, HOLO_COMMAND_NEXT, 1300U));
    assert(HOLO_PAGE_CLOCK == model.page);
    assert(holocubic_model_dispatch(&model, HOLO_COMMAND_NEXT, 1600U));
    assert(HOLO_PAGE_SPECTRUM == model.page);
    assert(0U == model.spectrum_mode);
    for (uint8_t mode = 1U; mode < HOLO_SPECTRUM_MODE_COUNT; ++mode) {
        assert(holocubic_model_dispatch(
            &model, HOLO_COMMAND_CONFIRM, 1600U + (uint32_t)mode * 300U));
        assert(mode == model.spectrum_mode);
    }
    assert(holocubic_model_dispatch(&model, HOLO_COMMAND_CONFIRM, 3400U));
    assert(0U == model.spectrum_mode);
    assert(holocubic_model_dispatch(&model, HOLO_COMMAND_NEXT, 3700U));
    assert(HOLO_PAGE_ANIMATION == model.page);
    assert(holocubic_model_dispatch(&model, HOLO_COMMAND_PREVIOUS, 1900U));
    assert(HOLO_PAGE_SPECTRUM == model.page);
    assert(0U == model.spectrum_mode);
}

static void test_touch(void)
{
    holocubic_touch_gesture_t gesture = {0};

    holocubic_touch_begin(&gesture, 100, 100);
    assert(HOLO_TOUCH_NONE == holocubic_touch_update(&gesture, 71, 100));
    assert(HOLO_TOUCH_NEXT == holocubic_touch_update(&gesture, 70, 100));
    assert(HOLO_TOUCH_NONE == holocubic_touch_update(&gesture, 30, 100));
    assert(HOLO_TOUCH_NONE == holocubic_touch_end(&gesture));

    holocubic_touch_begin(&gesture, 100, 100);
    assert(HOLO_TOUCH_PREVIOUS == holocubic_touch_update(&gesture, 130, 101));
    holocubic_touch_begin(&gesture, 100, 100);
    assert(HOLO_TOUCH_NONE == holocubic_touch_update(&gesture, 130, 130));
    assert(HOLO_TOUCH_CONFIRM == holocubic_touch_end(&gesture));
}

static void test_imu(void)
{
    holocubic_imu_gesture_t gesture = {0};

    holocubic_imu_init(&gesture);
    for (uint32_t index = 0U; index < HOLO_IMU_CALIBRATION_SAMPLES; ++index) {
        assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, -200,
                                                     index * 50U));
    }
    assert(gesture.calibrated && gesture.armed);
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 5100, -200, 1100U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 5100, -200, 1150U));
    assert(HOLO_IMU_CONFIRM == holocubic_imu_update(&gesture, 5100, -200, 1200U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, -200, 1800U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, -5300, 1850U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, -5300, 1900U));
    assert(HOLO_IMU_NEXT == holocubic_imu_update(&gesture, 100, -5300, 1950U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, -200, 2600U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, 5000, 2650U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, 5000, 2700U));
    assert(HOLO_IMU_PREVIOUS == holocubic_imu_update(&gesture, 100, 5000, 2750U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, -200, 3300U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, -5100, -200, 3350U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, -5100, -200, 3400U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, -5100, -200, 3450U));
}

static void test_imu_rearm_after_partial_return(void)
{
    holocubic_imu_gesture_t gesture = {0};

    holocubic_imu_init(&gesture);
    for (uint32_t index = 0U; index < HOLO_IMU_CALIBRATION_SAMPLES; ++index) {
        assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, -200,
                                                     index * 50U));
    }

    /* A partial return must release the axis that generated the last event. */
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, -5300, 1100U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, -5300, 1150U));
    assert(HOLO_IMU_NEXT == holocubic_imu_update(&gesture, 100, -5300, 1200U));
    assert(!gesture.armed);
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, 2800, 1800U));
    assert(gesture.armed);
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 5100, -200, 1900U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 5100, -200, 1950U));
    assert(HOLO_IMU_CONFIRM == holocubic_imu_update(&gesture, 5100, -200, 2000U));

    /* A direct reversal must also rearm without requiring an exact neutral sample. */
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, 5000, 2600U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, 5000, 2650U));
    assert(HOLO_IMU_PREVIOUS == holocubic_imu_update(&gesture, 100, 5000, 2700U));

    /* Holding the same tilt after an event must not repeat it. */
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, 5000, 2750U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, 5000, 2800U));
    assert(HOLO_IMU_NONE == holocubic_imu_update(&gesture, 100, 5000, 2850U));
}

static void test_time(void)
{
    holocubic_time_t time = {2024U, 2U, 29U, 23U, 59U, 59U};
    char clock_text[9] = {0};
    char date_text[11] = {0};
    char short_clock[8] = {0};
    bool completion_latched = false;

    assert(holocubic_time_is_leap_year(2000U));
    assert(holocubic_time_is_leap_year(2024U));
    assert(!holocubic_time_is_leap_year(2100U));
    assert(holocubic_time_is_valid(&time));
    assert(holocubic_time_format(&time, clock_text, sizeof(clock_text),
                                  date_text, sizeof(date_text)));
    assert(0 == strcmp("23:59:59", clock_text));
    assert(0 == strcmp("2024-02-29", date_text));
    assert(!holocubic_time_format(&time, short_clock, sizeof(short_clock),
                                   date_text, sizeof(date_text)));
    time.year = 2023U;
    assert(!holocubic_time_is_valid(&time));
    assert(!holocubic_time_format(&time, clock_text, sizeof(clock_text),
                                   date_text, sizeof(date_text)));
    time = (holocubic_time_t){2099U, 12U, 31U, 23U, 59U, 59U};
    assert(holocubic_time_is_valid(&time));
    time.year = 2100U;
    assert(!holocubic_time_is_valid(&time));

    assert(!holocubic_time_sync_should_apply(false, NULL));
    assert(!holocubic_time_sync_should_apply(false, &completion_latched));
    assert(!completion_latched);
    assert(holocubic_time_sync_should_apply(true, &completion_latched));
    assert(completion_latched);
    assert(!holocubic_time_sync_should_apply(true, &completion_latched));
    assert(completion_latched);
    assert(!holocubic_time_sync_should_apply(false, &completion_latched));
    assert(!completion_latched);
    assert(holocubic_time_sync_should_apply(true, &completion_latched));
}

static void test_weather(void)
{
    holocubic_weather_t weather = {0};
    holocubic_weather_t unchanged = {.state = HOLO_WEATHER_STALE, .revision = 9U};

    assert(holocubic_weather_parse(VALID_WEATHER_JSON, strlen(VALID_WEATHER_JSON),
                                   1000U, &weather));
    assert(HOLO_WEATHER_FRESH == weather.state);
    assert(35.5f == weather.temperature_c);
    assert(62U == weather.humidity_percent);
    assert(2 == weather.weather_code);
    assert(0 == strcmp("CLOUDY", holocubic_weather_code_text(weather.weather_code)));

    weather = unchanged;
    const char *missing = "{\"current\":{\"temperature_2m\":1}}";
    assert(!holocubic_weather_parse(missing, strlen(missing), 2000U, &weather));
    assert(9U == weather.revision);

    const char *unknown_code =
        "{\"current\":{\"time\":\"2026-08-13T12:30\","
        "\"temperature_2m\":35,\"relative_humidity_2m\":50,"
        "\"weather_code\":4},\"daily\":{\"temperature_2m_max\":[36],"
        "\"temperature_2m_min\":[28]}}";
    assert(!holocubic_weather_parse(unknown_code, strlen(unknown_code), 2000U,
                                     &weather));

    const char *bad_order =
        "{\"current\":{\"time\":\"2026-08-13T12:30\","
        "\"temperature_2m\":35,\"relative_humidity_2m\":50,"
        "\"weather_code\":0},\"daily\":{\"temperature_2m_max\":[20],"
        "\"temperature_2m_min\":[28]}}";
    assert(!holocubic_weather_parse(bad_order, strlen(bad_order), 2000U,
                                     &weather));

    const char *null_temperature =
        "{\"current\":{\"time\":\"2026-08-13T12:30\","
        "\"temperature_2m\":null,\"relative_humidity_2m\":50,"
        "\"weather_code\":0},\"daily\":{\"temperature_2m_max\":[36],"
        "\"temperature_2m_min\":[28]}}";
    assert(!holocubic_weather_parse(null_temperature, strlen(null_temperature),
                                     2000U, &weather));

    const char *misleading_nested_field =
        "{\"metadata\":{\"temperature_2m\":-99,\"time\":\"wrong\"},"
        "\"current\":{\"time\":\"2026-08-13T12:30\","
        "\"temperature_2m\":35.5,\"relative_humidity_2m\":62,"
        "\"weather_code\":2},\"daily\":{\"temperature_2m_max\":[38.0],"
        "\"temperature_2m_min\":[28.0]}}";
    assert(holocubic_weather_parse(misleading_nested_field,
                                    strlen(misleading_nested_field), 2000U,
                                    &weather));
    assert(35.5f == weather.temperature_c);
    assert(0 == strcmp("2026-08-13T12:30", weather.observed_at));

    const char *wrong_current_type =
        "{\"current\":[],\"daily\":{\"temperature_2m_max\":[38.0],"
        "\"temperature_2m_min\":[28.0]}}";
    assert(!holocubic_weather_parse(wrong_current_type,
                                     strlen(wrong_current_type), 2000U,
                                     &weather));
    assert(!holocubic_weather_parse(VALID_WEATHER_JSON,
                                     strlen(VALID_WEATHER_JSON) - 1U, 2000U,
                                     &weather));

    weather = (holocubic_weather_t){
        .state = HOLO_WEATHER_FRESH,
        .fetched_at_ms = 1000U,
        .revision = 1U,
    };
    holocubic_weather_mark_stale(&weather, 1000U + 30ULL * 60ULL * 1000ULL);
    assert(HOLO_WEATHER_FRESH == weather.state);
    holocubic_weather_mark_stale(&weather,
                                 1001U + 30ULL * 60ULL * 1000ULL);
    assert(HOLO_WEATHER_STALE == weather.state);
}

static void test_wifi(void)
{
    holocubic_wifi_credentials_t credentials = {0};
    char ssid[HOLO_WIFI_SSID_MAX_BYTES + 2U] = {0};
    char password[HOLO_WIFI_PASSWORD_MAX_BYTES + 2U] = {0};
    char hex_psk[HOLO_WIFI_PASSWORD_MAX_BYTES + 1U] = {0};
    char line[] = "hangzhou-wifi\r\n";

    memset(ssid, 's', HOLO_WIFI_SSID_MAX_BYTES);
    memset(password, 'p', 63U);
    memset(hex_psk, 'a', HOLO_WIFI_PASSWORD_MAX_BYTES);
    assert(!holocubic_wifi_credentials_set(&credentials, "a", ""));
    assert(holocubic_wifi_credentials_set(&credentials, ssid, "12345678"));
    ssid[HOLO_WIFI_SSID_MAX_BYTES] = 's';
    assert(!holocubic_wifi_credentials_set(&credentials, ssid, "12345678"));
    assert(!holocubic_wifi_credentials_set(&credentials, "wifi", "1234567"));
    assert(holocubic_wifi_credentials_set(&credentials, "wifi", password));
    assert(holocubic_wifi_credentials_set(&credentials, "wifi", hex_psk));
    hex_psk[HOLO_WIFI_PASSWORD_MAX_BYTES - 1U] = 'g';
    assert(!holocubic_wifi_credentials_set(&credentials, "wifi", hex_psk));
    password[63U] = 'p';
    assert(!holocubic_wifi_credentials_set(&credentials, "wifi", password));
    assert(!holocubic_wifi_credentials_set(&credentials, "   ", "12345678"));
    assert(!holocubic_wifi_credentials_set(&credentials, "wifi", "        "));
    assert(!holocubic_wifi_credentials_set(&credentials, "wi\x1f" "fi",
                                            "12345678"));
    assert(!holocubic_wifi_credentials_set(&credentials, "wifi",
                                            "1234567\x7f"));
    holocubic_wifi_trim_line(line);
    assert(0 == strcmp("hangzhou-wifi", line));

    FILE *file = tmpfile();
    char file_line[HOLO_WIFI_PASSWORD_MAX_BYTES + 3U] = {0};
    assert(NULL != file);
    assert(0 < fprintf(file, "%032d\r\n", 0));
    rewind(file);
    assert(holocubic_wifi_read_line(file, file_line, 35U));
    holocubic_wifi_trim_line(file_line);
    assert(HOLO_WIFI_SSID_MAX_BYTES == strlen(file_line));
    fclose(file);

    file = tmpfile();
    assert(NULL != file);
    assert(0 < fprintf(file, "1234567890123456789012345678901234\nnext\n"));
    rewind(file);
    assert(!holocubic_wifi_read_line(file, file_line, 35U));
    assert(holocubic_wifi_read_line(file, file_line, sizeof(file_line)));
    holocubic_wifi_trim_line(file_line);
    assert(0 == strcmp("next", file_line));
    fclose(file);
}

static void test_network_policy(void)
{
    holocubic_network_observation_t observation = {0};
    holocubic_network_schedule_t schedule = {0};

    assert(HOLO_NETWORK_OFFLINE == holocubic_network_decide(NULL));
    assert(HOLO_NETWORK_WAITING == holocubic_network_decide(&observation));
    observation.manager_start_failed = true;
    assert(HOLO_NETWORK_OFFLINE == holocubic_network_decide(&observation));
    observation.manager_start_failed = false;
    observation.stable_ready = true;
    assert(HOLO_NETWORK_WAITING == holocubic_network_decide(&observation));
    assert(HOLO_NETWORK_WAITING == holocubic_network_decide(&observation));
    observation.cellular_active = true;
    assert(HOLO_NETWORK_ONLINE == holocubic_network_decide(&observation));
    observation.stable_ready = false;
    assert(HOLO_NETWORK_WAITING == holocubic_network_decide(&observation));

    assert(!holocubic_network_weather_due(NULL, 0U));
    assert(holocubic_network_weather_due(&schedule, 1000U));
    holocubic_network_weather_result(&schedule, 1000U, false);
    assert(!holocubic_network_weather_due(
        &schedule, 1000U + HOLO_NETWORK_WEATHER_RETRY_MS - 1U));
    assert(holocubic_network_weather_due(
        &schedule, 1000U + HOLO_NETWORK_WEATHER_RETRY_MS));
    holocubic_network_weather_result(&schedule, 2000U, true);
    assert(!holocubic_network_weather_due(
        &schedule, 2000U + HOLO_NETWORK_WEATHER_REFRESH_MS - 1U));
    assert(holocubic_network_weather_due(
        &schedule, 2000U + HOLO_NETWORK_WEATHER_REFRESH_MS));
    holocubic_network_weather_result(&schedule, UINT64_MAX - 10U, true);
    assert(UINT64_MAX == schedule.next_fetch_ms);
}

static void test_ui_state(void)
{
    holocubic_ui_state_t state = {0};

    holocubic_ui_state_init(&state, false);
    assert(HOLO_DISPLAY_WIFI_CONFIG == state.mode);
    assert(state.connect_pending);
    assert(!holocubic_ui_main_input_enabled(&state));
    assert(holocubic_ui_wifi_input_enabled(&state));
    holocubic_ui_wifi_ready(&state);
    assert(HOLO_DISPLAY_HOME_HANDOFF == state.mode);
    holocubic_ui_home_handoff_complete(&state);
    assert(HOLO_DISPLAY_MAIN == state.mode);
    holocubic_ui_open_wifi(&state);
    assert(HOLO_DISPLAY_WIFI_CONFIG == state.mode);
    assert(!state.connect_pending);
    holocubic_ui_wifi_ready(&state);
    assert(HOLO_DISPLAY_WIFI_CONFIG == state.mode);
    holocubic_ui_open_keyboard(&state);
    assert(HOLO_DISPLAY_WIFI_KEYBOARD == state.mode);
    holocubic_ui_close_keyboard(&state);
    assert(HOLO_DISPLAY_WIFI_CONFIG == state.mode);
    holocubic_ui_connect_submitted(&state);
    assert(state.connect_pending && !holocubic_ui_main_input_enabled(&state));
    holocubic_ui_wifi_ready(&state);
    assert(HOLO_DISPLAY_HOME_HANDOFF == state.mode);
    assert(!holocubic_ui_main_input_enabled(&state));
    assert(!holocubic_ui_wifi_input_enabled(&state));
    holocubic_ui_home_handoff_complete(&state);
    assert(HOLO_DISPLAY_MAIN == state.mode);
    assert(holocubic_ui_main_input_enabled(&state));
    assert(!holocubic_ui_wifi_input_enabled(&state));
    holocubic_ui_open_wifi(&state);
    assert(HOLO_DISPLAY_WIFI_CONFIG == state.mode);

    holocubic_ui_state_init(&state, true);
    assert(HOLO_DISPLAY_MAIN == state.mode);
}

static void test_render_policy(void)
{
    const holocubic_render_policy_t policy =
        holocubic_render_policy_default();
    holocubic_present_gate_t gate = {0};
    size_t buffer_bytes = 0U;

    assert(80U == policy.chunk_lines);
    assert(policy.source_dma_required);
    assert(policy.horizontal_mirror);
    assert(policy.rotate_180);
    assert(239U == holocubic_render_mirror_x(true, 0U, 240U));
    assert(119U == holocubic_render_mirror_x(true, 120U, 240U));
    assert(0U == holocubic_render_mirror_x(true, 239U, 240U));
    assert(0U == holocubic_render_mirror_x(true, 240U, 240U));
    assert(0U == holocubic_render_mirror_x(true, 0U, 0U));
    assert(37U == holocubic_render_mirror_x(false, 37U, 240U));
    assert(0U == holocubic_render_sample_x(true, true, 0U, 240U));
    assert(120U == holocubic_render_sample_x(true, true, 120U, 240U));
    assert(239U == holocubic_render_sample_x(true, true, 239U, 240U));
    assert(239U == holocubic_render_sample_x(true, false, 0U, 240U));
    assert(0U == holocubic_render_sample_x(true, false, 239U, 240U));
    assert(0U == holocubic_render_sample_x(true, true, 240U, 240U));
    assert(239U == holocubic_render_sample_y(true, 0U, 240U));
    assert(119U == holocubic_render_sample_y(true, 120U, 240U));
    assert(0U == holocubic_render_sample_y(true, 239U, 240U));
    assert(37U == holocubic_render_sample_y(false, 37U, 240U));
    assert(0U == holocubic_render_sample_y(true, 240U, 240U));
    assert(holocubic_render_buffer_bytes(&policy, 240U, sizeof(uint16_t),
                                         &buffer_bytes));
    assert(38400U == buffer_bytes);
    assert(!holocubic_render_buffer_bytes(NULL, 240U, sizeof(uint16_t),
                                          &buffer_bytes));
    assert(!holocubic_render_buffer_bytes(&policy, 0U, sizeof(uint16_t),
                                          &buffer_bytes));
    assert(!holocubic_render_buffer_bytes(&policy, SIZE_MAX,
                                          sizeof(uint16_t), &buffer_bytes));

    assert(holocubic_present_gate_should_present(&gate, 1U, 1U));
    assert(!gate.valid);
    holocubic_present_gate_mark_presented(&gate, 1U, 1U);
    assert(gate.valid);
    assert(!holocubic_present_gate_should_present(&gate, 1U, 1U));
    assert(holocubic_present_gate_should_present(&gate, 1U, 2U));
    assert(holocubic_present_gate_should_present(&gate, 2U, 1U));
    holocubic_present_gate_invalidate(&gate);
    assert(!gate.valid);
    assert(holocubic_present_gate_should_present(&gate, 1U, 1U));
    assert(!holocubic_present_gate_should_present(NULL, 1U, 1U));
    holocubic_present_gate_mark_presented(NULL, 1U, 1U);
    holocubic_present_gate_invalidate(NULL);

    assert(123U == holocubic_content_revision(HOLO_PAGE_ANIMATION, true,
                                               123U, 9U, false, 0U, 0U,
                                               4567U, false));
    assert(45U == holocubic_content_revision(HOLO_PAGE_ANIMATION, false,
                                              123U, 9U, false, 0U, 0U,
                                              4567U, false));
    assert(14U == holocubic_content_revision(HOLO_PAGE_WEATHER, false,
                                              123U, 14U, false, 0U, 0U,
                                              4567U, true));
    assert((((12U << 1U) | 1U)) ==
           holocubic_content_revision(HOLO_PAGE_CLOCK, false, 123U, 14U,
                                      false, 0U, 0U, 12000U, true));
    assert((12U << 1U) ==
           holocubic_content_revision(HOLO_PAGE_CLOCK, false, 123U, 14U,
                                      false, 0U, 0U, 12099U, false));
    assert(((7U << 3U) | 2U) ==
           holocubic_content_revision(HOLO_PAGE_SPECTRUM, false, 123U, 14U,
                                      true, 7U, 2U, 4567U, true));
    assert(45U == holocubic_content_revision(HOLO_PAGE_SPECTRUM, false,
                                              123U, 14U, false, 7U, 2U,
                                              4567U, true));
}

static void test_wifi_buffer_policy(void)
{
    const holocubic_wifi_buffer_policy_t policy =
        holocubic_wifi_buffer_policy_default();
    holocubic_wifi_buffer_plan_t plan = {0};

    assert(holocubic_wifi_buffer_plan_mode(&policy, true, &plan));
    assert(plan.valid && plan.double_buffer);
    assert(40U == plan.lines && 12800U == plan.pixels);
    assert(25600U == plan.bytes_per_buffer);

    assert(holocubic_wifi_buffer_plan_mode(&policy, false, &plan));
    assert(plan.valid && !plan.double_buffer);
    assert(16U == plan.lines && 5120U == plan.pixels);
    assert(10240U == plan.bytes_per_buffer);

    assert(!holocubic_wifi_buffer_plan_mode(NULL, true, &plan));
    assert(!holocubic_wifi_buffer_plan_mode(&policy, true, NULL));
}

static void test_ui_clock(void)
{
    holocubic_ui_clock_t clock = {0};
    uint32_t total_ms = 0U;

    holocubic_ui_clock_init(&clock);
    assert(0U == holocubic_ui_clock_advance(&clock, 1000U));
    total_ms += holocubic_ui_clock_advance(&clock, 1010U);
    total_ms += holocubic_ui_clock_advance(&clock, 1020U);
    total_ms += holocubic_ui_clock_advance(&clock, 1030U);
    assert(30U == total_ms);
    assert(0U == holocubic_ui_clock_advance(&clock, 1030U));

    assert(0U == holocubic_ui_clock_advance(&clock, 900U));
    assert(10U == holocubic_ui_clock_advance(&clock, 910U));
    assert(UINT32_MAX == holocubic_ui_clock_advance(
                             &clock, 910ULL + UINT32_MAX + 1ULL));
    assert(0U == holocubic_ui_clock_advance(NULL, 1000U));
    holocubic_ui_clock_init(NULL);
}

static void test_visual_ui(void)
{
    static uint16_t guarded_canvas[HOLO_VISUAL_CANVAS_PIXELS + 2U];
    static uint16_t asset_pixels[HOLO_VISUAL_ASSET_PIXELS];
    uint16_t *canvas = &guarded_canvas[1U];
    holocubic_visual_bitmap_t bitmap = {
        .pixels = asset_pixels,
        .pixel_count = HOLO_VISUAL_ASSET_PIXELS,
        .width = HOLO_VISUAL_ASSET_WIDTH,
        .height = HOLO_VISUAL_ASSET_HEIGHT,
    };
    holocubic_weather_t weather = {
        .state = HOLO_WEATHER_FRESH,
        .temperature_c = 35.5f,
        .high_c = 38.0f,
        .low_c = 28.0f,
        .humidity_percent = 62U,
        .weather_code = 2,
    };

    assert(HOLO_WEATHER_VISUAL_CLEAR == holocubic_visual_weather_kind(0));
    assert(HOLO_WEATHER_VISUAL_CLOUDY == holocubic_visual_weather_kind(2));
    assert(HOLO_WEATHER_VISUAL_OVERCAST == holocubic_visual_weather_kind(3));
    assert(HOLO_WEATHER_VISUAL_FOG == holocubic_visual_weather_kind(45));
    assert(HOLO_WEATHER_VISUAL_RAIN == holocubic_visual_weather_kind(61));
    assert(HOLO_WEATHER_VISUAL_SNOW == holocubic_visual_weather_kind(71));
    assert(HOLO_WEATHER_VISUAL_STORM == holocubic_visual_weather_kind(95));
    assert(HOLO_WEATHER_VISUAL_OFFLINE == holocubic_visual_weather_kind(4));

    memset(asset_pixels, 0x34, sizeof(asset_pixels));
    guarded_canvas[0U] = 0xA55AU;
    guarded_canvas[HOLO_VISUAL_CANVAS_PIXELS + 1U] = 0x5AA5U;
    assert(holocubic_visual_draw_weather(canvas, HOLO_VISUAL_CANVAS_PIXELS,
                                         &weather, &bitmap));
    assert(0x3434U == canvas[(size_t)HOLO_VISUAL_ASSET_Y *
                             HOLO_VISUAL_CANVAS_WIDTH +
                             HOLO_VISUAL_ASSET_X]);
    assert(0x0000U == canvas[0U]);
    assert(0xA55AU == guarded_canvas[0U]);
    assert(0x5AA5U == guarded_canvas[HOLO_VISUAL_CANVAS_PIXELS + 1U]);

    assert(holocubic_visual_draw_weather(canvas, HOLO_VISUAL_CANVAS_PIXELS,
                                         NULL, &bitmap));
    assert(holocubic_visual_draw_clock(canvas, HOLO_VISUAL_CANVAS_PIXELS,
                                       "23:59:42", "2026-08-14", true,
                                       &bitmap));
    assert(0x3434U == canvas[(size_t)HOLO_VISUAL_ASSET_Y *
                             HOLO_VISUAL_CANVAS_WIDTH +
                             HOLO_VISUAL_ASSET_X]);
    assert(holocubic_visual_draw_clock(canvas, HOLO_VISUAL_CANVAS_PIXELS,
                                       NULL, NULL, false, &bitmap));

    bitmap.pixel_count--;
    assert(!holocubic_visual_draw_weather(canvas, HOLO_VISUAL_CANVAS_PIXELS,
                                          &weather, &bitmap));
    assert(!holocubic_visual_draw_clock(canvas, HOLO_VISUAL_CANVAS_PIXELS,
                                        "12:00:00", "2026-08-14", true,
                                        &bitmap));
    assert(!holocubic_visual_draw_weather(NULL, HOLO_VISUAL_CANVAS_PIXELS,
                                          &weather, &bitmap));
    assert(!holocubic_visual_draw_clock(canvas,
                                        HOLO_VISUAL_CANVAS_PIXELS - 1U,
                                        "12:00:00", "2026-08-14", true,
                                        &bitmap));
}

int main(void)
{
    test_layout();
    test_frame_format();
    test_periodic();
    test_spi_policy();
    test_startup_plan();
    test_model();
    test_touch();
    test_imu();
    test_imu_rearm_after_partial_return();
    test_time();
    test_weather();
    test_wifi();
    test_network_policy();
    test_ui_state();
    test_render_policy();
    test_ui_clock();
    test_visual_ui();
    test_wifi_buffer_policy();
    puts("holocubic_core_test: PASS");
    return 0;
}
