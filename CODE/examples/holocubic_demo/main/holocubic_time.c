#include "holocubic_time.h"

#include <stddef.h>
#include <stdio.h>

bool holocubic_time_is_leap_year(uint16_t year)
{
    return (0U == (year % 400U)) ||
           (0U == (year % 4U) && 0U != (year % 100U));
}

bool holocubic_time_is_valid(const holocubic_time_t *time)
{
    static const uint8_t days_in_month[] = {31U, 28U, 31U, 30U, 31U, 30U,
                                             31U, 31U, 30U, 31U, 30U, 31U};
    uint8_t max_day = 0U;

    if (NULL == time || time->year < 2000U || time->year > 2099U ||
        time->month < 1U || time->month > 12U || time->hours > 23U ||
        time->minutes > 59U || time->seconds > 59U) {
        return false;
    }
    max_day = days_in_month[time->month - 1U];
    if (2U == time->month && holocubic_time_is_leap_year(time->year)) {
        max_day++;
    }
    return time->day >= 1U && time->day <= max_day;
}

bool holocubic_time_format(const holocubic_time_t *time,
                           char *clock_text,
                           size_t clock_text_size,
                           char *date_text,
                           size_t date_text_size)
{
    int clock_length = 0;
    int date_length = 0;

    if (!holocubic_time_is_valid(time) || NULL == clock_text ||
        NULL == date_text || 0U == clock_text_size || 0U == date_text_size) {
        return false;
    }
    clock_text[0] = '\0';
    date_text[0] = '\0';
    clock_length = snprintf(clock_text, clock_text_size, "%02u:%02u:%02u",
                            (unsigned int)time->hours,
                            (unsigned int)time->minutes,
                            (unsigned int)time->seconds);
    date_length = snprintf(date_text, date_text_size, "%04u-%02u-%02u",
                           (unsigned int)time->year,
                           (unsigned int)time->month,
                           (unsigned int)time->day);
    if (clock_length < 0 || date_length < 0 ||
        (size_t)clock_length >= clock_text_size ||
        (size_t)date_length >= date_text_size) {
        clock_text[0] = '\0';
        date_text[0] = '\0';
        return false;
    }
    return true;
}

bool holocubic_time_sync_should_apply(bool sync_completed,
                                      bool *completion_latched)
{
    if (NULL == completion_latched) {
        return false;
    }
    if (!sync_completed) {
        *completion_latched = false;
        return false;
    }
    if (*completion_latched) {
        return false;
    }
    *completion_latched = true;
    return true;
}
