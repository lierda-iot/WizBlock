#include "holocubic_spectrum_visual_math.h"

#include <math.h>

#define HOLO_SPECTRUM_LEVEL_MIN 0.0f
#define HOLO_SPECTRUM_LEVEL_MAX 1.0f
#define HOLO_SPECTRUM_RED_BASE 30.0f
#define HOLO_SPECTRUM_RED_SPAN 190.0f
#define HOLO_SPECTRUM_GREEN_BASE 50.0f
#define HOLO_SPECTRUM_GREEN_SPAN 180.0f
#define HOLO_SPECTRUM_GREEN_BIAS_SCALE 0.3f
#define HOLO_SPECTRUM_BLUE_BASE 90.0f
#define HOLO_SPECTRUM_BLUE_SPAN 165.0f
#define HOLO_SPECTRUM_COLOR_MIN 0.0f
#define HOLO_SPECTRUM_COLOR_MAX 255.0f

static float clamp_unit(float value)
{
    if (!isfinite(value) || HOLO_SPECTRUM_LEVEL_MIN > value) {
        return HOLO_SPECTRUM_LEVEL_MIN;
    }
    if (HOLO_SPECTRUM_LEVEL_MAX < value) {
        return HOLO_SPECTRUM_LEVEL_MAX;
    }
    return value;
}

static uint8_t clamp_color_component(float value)
{
    if (!isfinite(value) || HOLO_SPECTRUM_COLOR_MIN >= value) {
        return 0U;
    }
    if (HOLO_SPECTRUM_COLOR_MAX <= value) {
        return UINT8_MAX;
    }
    return (uint8_t)value;
}

holocubic_spectrum_rgb_t holocubic_spectrum_energy_rgb(float level,
                                                        uint8_t bias)
{
    const float clamped = clamp_unit(level);
    const float bias_value = (float)bias;

    return (holocubic_spectrum_rgb_t){
        .red = clamp_color_component(
            HOLO_SPECTRUM_RED_BASE +
            clamped * (HOLO_SPECTRUM_RED_SPAN + bias_value)),
        .green = clamp_color_component(
            HOLO_SPECTRUM_GREEN_BASE +
            clamped * (HOLO_SPECTRUM_GREEN_SPAN -
                       bias_value * HOLO_SPECTRUM_GREEN_BIAS_SCALE)),
        .blue = clamp_color_component(
            HOLO_SPECTRUM_BLUE_BASE + clamped * HOLO_SPECTRUM_BLUE_SPAN),
    };
}
