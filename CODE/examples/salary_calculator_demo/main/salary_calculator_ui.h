#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    char salary_text[16];
    char sound_interval_text[16];
    uint8_t work_start_hour;
    uint8_t work_start_minute;
    uint8_t work_end_hour;
    uint8_t work_end_minute;
    uint16_t current_year;
    uint8_t current_month;
    uint8_t current_day;
    uint8_t current_hour;
    uint8_t current_minute;
    uint8_t current_second;
} salary_ui_settings_state_t;

typedef struct {
    char salary_text[16];
    char sound_interval_text[16];
    uint8_t work_start_hour;
    uint8_t work_start_minute;
    uint8_t work_end_hour;
    uint8_t work_end_minute;
    uint16_t current_year;
    uint8_t current_month;
    uint8_t current_day;
    uint8_t current_hour;
    uint8_t current_minute;
    uint8_t current_second;
    bool current_time_dirty;
} salary_ui_form_t;

typedef struct {
    char amount_text[32];
    char progress_text[32];
} salary_ui_money_state_t;

typedef void (*salary_ui_confirm_cb_t)(const salary_ui_form_t *form, void *user_ctx);
typedef void (*salary_ui_back_cb_t)(void *user_ctx);

typedef struct {
    salary_ui_confirm_cb_t on_confirm;
    salary_ui_back_cb_t on_back;
    void *user_ctx;
} salary_ui_callbacks_t;

esp_err_t salary_calculator_ui_init(const salary_ui_callbacks_t *callbacks);
esp_err_t salary_calculator_ui_apply_settings(const salary_ui_settings_state_t *state);
esp_err_t salary_calculator_ui_update_rtc_display(const char *rtc_text, bool rtc_power_lost);
esp_err_t salary_calculator_ui_set_message(const char *text, bool is_error);
esp_err_t salary_calculator_ui_update_money(const salary_ui_money_state_t *state);
esp_err_t salary_calculator_ui_show_money_page(void);
esp_err_t salary_calculator_ui_show_settings_page(void);
esp_err_t salary_calculator_ui_trigger_coin_burst(void);
