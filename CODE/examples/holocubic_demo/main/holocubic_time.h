#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
} holocubic_time_t;

bool holocubic_time_is_valid(const holocubic_time_t *time);
bool holocubic_time_is_leap_year(uint16_t year);
bool holocubic_time_format(const holocubic_time_t *time,
                           char *clock_text,
                           size_t clock_text_size,
                           char *date_text,
                           size_t date_text_size);
bool holocubic_time_sync_should_apply(bool sync_completed,
                                      bool *completion_latched);
