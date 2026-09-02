#include "audio_dual_mic_doa_filter.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#define TEST_EPSILON 0.01f

static void assert_near(float expected, float actual)
{
    assert(fabsf(expected - actual) < TEST_EPSILON);
}

static void test_single_frame_outlier_is_rejected(void)
{
    doa_angle_filter_t filter = {0};
    float filtered_deg = 0.0f;

    for (uint32_t index = 0U; index < DOA_ANGLE_FILTER_WINDOW_SIZE; index++) {
        assert(doa_angle_filter_update(&filter, 90.0f, &filtered_deg));
    }

    assert(doa_angle_filter_update(&filter, 20.0f, &filtered_deg));
    assert_near(90.0f, filtered_deg);
    assert(doa_angle_filter_update(&filter, 170.0f, &filtered_deg));
    assert_near(90.0f, filtered_deg);
}

static void test_real_direction_change_remains_responsive(void)
{
    doa_angle_filter_t filter = {0};
    float filtered_deg = 0.0f;

    for (uint32_t index = 0U; index < DOA_ANGLE_FILTER_WINDOW_SIZE; index++) {
        assert(doa_angle_filter_update(&filter, 90.0f, &filtered_deg));
    }
    for (uint32_t index = 0U; index < 12U; index++) {
        assert(doa_angle_filter_update(&filter, 150.0f, &filtered_deg));
    }

    assert(145.0f < filtered_deg);
    assert(150.0f >= filtered_deg);
}

static void test_direction_hysteresis_prevents_boundary_flapping(void)
{
    doa_filter_direction_t direction = DOA_FILTER_DIRECTION_CENTER;

    direction = doa_direction_filter_update(direction, 76.0f);
    assert(DOA_FILTER_DIRECTION_CENTER == direction);
    direction = doa_direction_filter_update(direction, 74.0f);
    assert(DOA_FILTER_DIRECTION_RIGHT == direction);
    direction = doa_direction_filter_update(direction, 78.0f);
    assert(DOA_FILTER_DIRECTION_RIGHT == direction);
    direction = doa_direction_filter_update(direction, 80.0f);
    assert(DOA_FILTER_DIRECTION_CENTER == direction);

    direction = doa_direction_filter_update(direction, 104.0f);
    assert(DOA_FILTER_DIRECTION_CENTER == direction);
    direction = doa_direction_filter_update(direction, 106.0f);
    assert(DOA_FILTER_DIRECTION_LEFT == direction);
    direction = doa_direction_filter_update(direction, 102.0f);
    assert(DOA_FILTER_DIRECTION_LEFT == direction);
    direction = doa_direction_filter_update(direction, 100.0f);
    assert(DOA_FILTER_DIRECTION_CENTER == direction);
}

static void test_filter_reset_discards_previous_direction_history(void)
{
    doa_angle_filter_t filter = {0};
    float filtered_deg = 0.0f;

    for (uint32_t index = 0U; index < DOA_ANGLE_FILTER_WINDOW_SIZE; index++) {
        assert(doa_angle_filter_update(&filter, 30.0f, &filtered_deg));
    }
    doa_angle_filter_reset(&filter);

    assert(doa_angle_filter_update(&filter, 150.0f, &filtered_deg));
    assert_near(150.0f, filtered_deg);
}

static void test_short_zero_angle_gap_is_not_added_to_filter(void)
{
    doa_angle_filter_t filter = {0};
    float filtered_deg = 0.0f;

    for (uint32_t index = 0U; index < DOA_ANGLE_FILTER_WINDOW_SIZE; index++) {
        assert(doa_angle_filter_update(&filter, 90.0f, &filtered_deg));
    }

    assert(!doa_angle_filter_update(&filter, 0.0f, &filtered_deg));
    assert_near(90.0f, filtered_deg);
    assert(!doa_angle_filter_update(&filter, 0.0f, &filtered_deg));
    assert_near(90.0f, filtered_deg);

    assert(doa_angle_filter_update(&filter, 0.0f, &filtered_deg));
    assert_near(90.0f, filtered_deg);
    assert(doa_angle_filter_update(&filter, 0.0f, &filtered_deg));
    assert(doa_angle_filter_update(&filter, 0.0f, &filtered_deg));
    assert(90.0f > filtered_deg);
}

int main(void)
{
    test_single_frame_outlier_is_rejected();
    test_real_direction_change_remains_responsive();
    test_direction_hysteresis_prevents_boundary_flapping();
    test_filter_reset_discards_previous_direction_history();
    test_short_zero_angle_gap_is_not_added_to_filter();
    puts("audio_dual_mic_doa_filter_test: PASS");
    return 0;
}
