#ifndef HOLOCUBIC_SPECTRUM_VISUAL_MATH_H
#define HOLOCUBIC_SPECTRUM_VISUAL_MATH_H

#include <stdint.h>

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} holocubic_spectrum_rgb_t;

holocubic_spectrum_rgb_t holocubic_spectrum_energy_rgb(float level,
                                                        uint8_t bias);

#endif
