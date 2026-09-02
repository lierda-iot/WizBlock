#include "salary_calculator_logic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static bool salary_is_leap_year(uint16_t year)
{
    return ((0U == (year % 4U)) && (0U != (year % 100U))) ||
           (0U == (year % 400U));
}

static uint8_t salary_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days_by_month[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };
    uint8_t days = 0;

    if (1U > month || 12U < month) {
        return 0;
    }

    days = days_by_month[month - 1U];
    if ((2U == month) && salary_is_leap_year(year)) {
        days++;
    }
    return days;
}

static bool salary_clock_to_tm(const salary_clock_t *clock, struct tm *timeinfo)
{
    if (NULL == clock || NULL == timeinfo) {
        return false;
    }
    if (!salary_clock_validate(clock)) {
        return false;
    }

    memset(timeinfo, 0, sizeof(*timeinfo));
    timeinfo->tm_year = (int)clock->year - 1900;
    timeinfo->tm_mon = (int)clock->month - 1;
    timeinfo->tm_mday = (int)clock->day;
    timeinfo->tm_hour = (int)clock->hour;
    timeinfo->tm_min = (int)clock->minute;
    timeinfo->tm_sec = (int)clock->second;
    timeinfo->tm_isdst = -1;
    return true;
}

static bool salary_tm_to_clock(const struct tm *timeinfo, salary_clock_t *clock)
{
    if (NULL == timeinfo || NULL == clock) {
        return false;
    }

    clock->year = (uint16_t)(timeinfo->tm_year + 1900);
    clock->month = (uint8_t)(timeinfo->tm_mon + 1);
    clock->day = (uint8_t)timeinfo->tm_mday;
    clock->hour = (uint8_t)timeinfo->tm_hour;
    clock->minute = (uint8_t)timeinfo->tm_min;
    clock->second = (uint8_t)timeinfo->tm_sec;
    return true;
}

bool salary_settings_validate(const salary_settings_t *settings)
{
    if (NULL == settings) {
        return false;
    }
    if (settings->month_salary_yuan <= 0) {
        return false;
    }
    if (settings->sound_interval_yuan <= 0) {
        return false;
    }
    if (settings->work_start_minutes >= (24U * 60U) || settings->work_end_minutes >= (24U * 60U)) {
        return false;
    }
    if (settings->work_end_minutes <= settings->work_start_minutes) {
        return false;
    }
    return true;
}

bool salary_clock_validate(const salary_clock_t *clock)
{
    uint8_t days_in_month = 0;

    if (NULL == clock) {
        return false;
    }
    if (SALARY_RTC_YEAR_MIN > clock->year || SALARY_RTC_YEAR_MAX < clock->year) {
        return false;
    }
    if (1U > clock->month || 12U < clock->month) {
        return false;
    }

    days_in_month = salary_days_in_month(clock->year, clock->month);
    if (1U > clock->day || days_in_month < clock->day) {
        return false;
    }
    if (23U < clock->hour || 59U < clock->minute || 59U < clock->second) {
        return false;
    }
    return true;
}

bool salary_parse_month_salary_yuan(const char *text, int64_t *out_yuan)
{
    char *end = NULL;
    long long value = 0;

    if (NULL == text || NULL == out_yuan) {
        return false;
    }
    if ('\0' == text[0]) {
        return false;
    }

    value = strtoll(text, &end, 10);
    if (end == text || '\0' != *end || value <= 0) {
        return false;
    }

    *out_yuan = (int64_t)value;
    return true;
}

void salary_format_currency_cny(int64_t amount_cents, char *buf, size_t buf_len)
{
    int64_t yuan = 0;
    int64_t cents = 0;

    if (NULL == buf || 0 == buf_len) {
        return;
    }

    if (amount_cents < 0) {
        amount_cents = 0;
    }

    yuan = amount_cents / 100;
    cents = amount_cents % 100;
    snprintf(buf, buf_len, "CNY %lld.%02lld", (long long)yuan, (long long)cents);
}

void salary_format_hhmm_from_minutes(uint16_t minutes, char *buf, size_t buf_len)
{
    uint16_t hour = 0;
    uint16_t minute = 0;

    if (NULL == buf || 0 == buf_len) {
        return;
    }

    hour = (uint16_t)(minutes / 60U);
    minute = (uint16_t)(minutes % 60U);
    snprintf(buf, buf_len, "%02u:%02u", (unsigned)hour, (unsigned)minute);
}

void salary_format_clock(const salary_clock_t *clock, bool with_seconds, char *buf, size_t buf_len)
{
    if (NULL == clock || NULL == buf || 0 == buf_len) {
        return;
    }

    if (with_seconds) {
        snprintf(buf, buf_len, "%04u-%02u-%02u %02u:%02u:%02u",
                 (unsigned)clock->year, (unsigned)clock->month, (unsigned)clock->day,
                 (unsigned)clock->hour, (unsigned)clock->minute, (unsigned)clock->second);
    } else {
        snprintf(buf, buf_len, "%04u-%02u-%02u %02u:%02u",
                 (unsigned)clock->year, (unsigned)clock->month, (unsigned)clock->day,
                 (unsigned)clock->hour, (unsigned)clock->minute);
    }
}

bool salary_clock_add_seconds(const salary_clock_t *base, int64_t delta_seconds, salary_clock_t *out_clock)
{
    struct tm timeinfo = {0};
    time_t epoch = 0;
    struct tm result = {0};

    if (!salary_clock_to_tm(base, &timeinfo) || NULL == out_clock) {
        return false;
    }

    epoch = mktime(&timeinfo);
    if ((time_t)-1 == epoch) {
        return false;
    }

    epoch += (time_t)delta_seconds;
    if (NULL == localtime_r(&epoch, &result)) {
        return false;
    }

    return salary_tm_to_clock(&result, out_clock);
}

void salary_calc_compute_result(const salary_settings_t *settings,
                                const salary_clock_t *clock,
                                salary_calc_result_t *out_result)
{
    uint32_t current_seconds = 0;
    uint32_t start_seconds = 0;
    uint32_t end_seconds = 0;
    uint32_t duration_seconds = 0;
    uint32_t elapsed_seconds = 0;
    int64_t numerator = 0;

    if (NULL == settings || NULL == clock || NULL == out_result) {
        return;
    }

    memset(out_result, 0, sizeof(*out_result));
    if (!salary_settings_validate(settings)) {
        return;
    }

    current_seconds = (uint32_t)clock->hour * 3600U +
                      (uint32_t)clock->minute * 60U +
                      (uint32_t)clock->second;
    start_seconds = (uint32_t)settings->work_start_minutes * 60U;
    end_seconds = (uint32_t)settings->work_end_minutes * 60U;
    duration_seconds = end_seconds - start_seconds;

    out_result->total_seconds = duration_seconds;

    if (current_seconds <= start_seconds) {
        out_result->before_work = true;
        return;
    }

    if (current_seconds >= end_seconds) {
        elapsed_seconds = duration_seconds;
        out_result->after_work = true;
    } else {
        elapsed_seconds = current_seconds - start_seconds;
        out_result->active_work = true;
    }

    out_result->elapsed_seconds = elapsed_seconds;
    out_result->progress_percent = (uint8_t)((elapsed_seconds * 100U) / duration_seconds);

    numerator = settings->month_salary_yuan * 100LL * (int64_t)elapsed_seconds;
    out_result->earned_cents = numerator / (22LL * (int64_t)duration_seconds);
}
