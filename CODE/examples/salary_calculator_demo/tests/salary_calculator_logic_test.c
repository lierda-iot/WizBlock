#include "salary_calculator_logic.h"

#include <assert.h>
#include <stdio.h>

static void test_clock_validation(void)
{
    const salary_clock_t leap_day = {
        .year = 2028,
        .month = 2,
        .day = 29,
        .hour = 23,
        .minute = 59,
        .second = 59,
    };
    salary_clock_t invalid_day = leap_day;
    salary_clock_t out_of_range = leap_day;

    assert(salary_clock_validate(&leap_day));

    invalid_day.year = 2027;
    assert(!salary_clock_validate(&invalid_day));

    invalid_day.year = 2028;
    invalid_day.month = 4;
    invalid_day.day = 31;
    assert(!salary_clock_validate(&invalid_day));

    out_of_range.year = 2100;
    assert(!salary_clock_validate(&out_of_range));
}

static void test_amount_changes_each_second(void)
{
    const salary_settings_t settings = {
        .month_salary_yuan = 792,
        .sound_interval_yuan = 1,
        .work_start_minutes = 9U * 60U,
        .work_end_minutes = 10U * 60U,
    };
    salary_clock_t clock = {
        .year = 2026,
        .month = 7,
        .day = 17,
        .hour = 9,
        .minute = 0,
        .second = 1,
    };
    salary_calc_result_t result = {0};

    salary_calc_compute_result(&settings, &clock, &result);
    assert(1 == result.earned_cents);

    clock.second = 2;
    salary_calc_compute_result(&settings, &clock, &result);
    assert(2 == result.earned_cents);
}

static void test_sound_interval_validation(void)
{
    salary_settings_t settings = {
        .month_salary_yuan = 15000,
        .sound_interval_yuan = 1,
        .work_start_minutes = 9U * 60U,
        .work_end_minutes = 18U * 60U,
    };

    assert(salary_settings_validate(&settings));

    settings.sound_interval_yuan = 0;
    assert(!salary_settings_validate(&settings));
}

int main(void)
{
    test_clock_validation();
    test_amount_changes_each_second();
    test_sound_interval_validation();
    puts("salary_calculator_logic_test: PASS");
    return 0;
}
