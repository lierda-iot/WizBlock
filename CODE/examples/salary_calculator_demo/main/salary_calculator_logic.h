#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SALARY_RTC_YEAR_MIN 2000U
#define SALARY_RTC_YEAR_MAX 2099U

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} salary_clock_t;

typedef struct {
    int64_t month_salary_yuan;
    int64_t sound_interval_yuan;
    uint16_t work_start_minutes;
    uint16_t work_end_minutes;
} salary_settings_t;

typedef struct {
    int64_t earned_cents;
    uint8_t progress_percent;
    bool before_work;
    bool after_work;
    bool active_work;
    uint32_t elapsed_seconds;
    uint32_t total_seconds;
} salary_calc_result_t;

bool salary_settings_validate(const salary_settings_t *settings);
bool salary_clock_validate(const salary_clock_t *clock);
bool salary_parse_month_salary_yuan(const char *text, int64_t *out_yuan);
void salary_format_currency_cny(int64_t amount_cents, char *buf, size_t buf_len);
void salary_format_hhmm_from_minutes(uint16_t minutes, char *buf, size_t buf_len);
void salary_format_clock(const salary_clock_t *clock, bool with_seconds, char *buf, size_t buf_len);
bool salary_clock_add_seconds(const salary_clock_t *base, int64_t delta_seconds, salary_clock_t *out_clock);
void salary_calc_compute_result(const salary_settings_t *settings,
                                const salary_clock_t *clock,
                                salary_calc_result_t *out_result);
