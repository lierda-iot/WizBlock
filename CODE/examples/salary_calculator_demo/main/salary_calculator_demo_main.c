#include "salary_calculator_logic.h"
#include "salary_calculator_ui.h"

#include "aip8563_rtc.h"
#include "board_laiwfs300.h"
#include "launcher_return.h"

#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "salary_calc_demo";

#define SETTINGS_NAMESPACE         "salary_calc"
#define SETTINGS_KEY               "config"
#define DEFAULT_MONTH_SALARY_YUAN  15000
#define DEFAULT_SOUND_INTERVAL_YUAN 1
#define DEFAULT_WORK_START_MINUTES (9 * 60)
#define DEFAULT_WORK_END_MINUTES   (18 * 60)
#define AUDIO_OUTPUT_VOL           70
#define COIN_SOUND_FILE_PATH       "/spiffs_data/coin_burst.wav"
#define FALLBACK_TONE_HZ           1800
#define FALLBACK_TONE_MS           120
#define APP_UPDATE_INTERVAL_MS     1000
#define AUDIO_TASK_STACK           4096

typedef struct {
    int64_t month_salary_yuan;
    int64_t sound_interval_yuan;
    uint16_t work_start_minutes;
    uint16_t work_end_minutes;
} persisted_settings_t;

typedef struct {
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
} wav_info_t;

static SemaphoreHandle_t s_state_mutex;
static salary_settings_t s_settings = {
    .month_salary_yuan = DEFAULT_MONTH_SALARY_YUAN,
    .sound_interval_yuan = DEFAULT_SOUND_INTERVAL_YUAN,
    .work_start_minutes = DEFAULT_WORK_START_MINUTES,
    .work_end_minutes = DEFAULT_WORK_END_MINUTES,
};
static salary_clock_t s_clock_base = {
    .year = 2026,
    .month = 7,
    .day = 17,
    .hour = 9,
    .minute = 0,
    .second = 0,
};
static int64_t s_clock_base_us;
static bool s_money_page_active;
static bool s_rtc_power_lost;
static bool s_coin_wav_available;
static esp_codec_dev_handle_t s_out_dev;
static int64_t s_last_sound_bucket;
static TaskHandle_t s_audio_task_handle;

static bool app_lock(void)
{
    if (NULL == s_state_mutex) {
        return false;
    }
    return (pdTRUE == xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(1000)));
}

static void app_unlock(void)
{
    if (NULL != s_state_mutex) {
        xSemaphoreGive(s_state_mutex);
    }
}

static salary_clock_t rtc_to_clock(const aip8563_time_t *rtc_time)
{
    salary_clock_t clock = {
        .year = 2000U + rtc_time->year,
        .month = rtc_time->month,
        .day = rtc_time->day,
        .hour = rtc_time->hours,
        .minute = rtc_time->minutes,
        .second = rtc_time->seconds,
    };

    return clock;
}

static aip8563_time_t clock_to_rtc(const salary_clock_t *clock)
{
    aip8563_time_t rtc_time = {
        .year = (uint8_t)(clock->year % 100U),
        .month = clock->month,
        .day = clock->day,
        .hours = clock->hour,
        .minutes = clock->minute,
        .seconds = clock->second,
        .weekday = 0,
    };

    return rtc_time;
}

static salary_clock_t get_current_clock(void)
{
    salary_clock_t current = {0};
    salary_clock_t base = {0};
    int64_t base_us = 0;
    int64_t now_us = esp_timer_get_time();

    if (app_lock()) {
        base = s_clock_base;
        base_us = s_clock_base_us;
        app_unlock();
    }

    if (!salary_clock_add_seconds(&base, (now_us - base_us) / 1000000LL, &current)) {
        current = base;
    }
    return current;
}

static void set_clock_base(const salary_clock_t *clock)
{
    if (NULL == clock) {
        return;
    }

    if (app_lock()) {
        s_clock_base = *clock;
        s_clock_base_us = esp_timer_get_time();
        app_unlock();
    }
}

static void load_default_settings(void)
{
    if (app_lock()) {
        s_settings.month_salary_yuan = DEFAULT_MONTH_SALARY_YUAN;
        s_settings.sound_interval_yuan = DEFAULT_SOUND_INTERVAL_YUAN;
        s_settings.work_start_minutes = DEFAULT_WORK_START_MINUTES;
        s_settings.work_end_minutes = DEFAULT_WORK_END_MINUTES;
        app_unlock();
    }
}

static esp_err_t load_settings_from_nvs(void)
{
    nvs_handle_t nvs = 0;
    persisted_settings_t persisted = {0};
    size_t len = sizeof(persisted);
    esp_err_t ret = ESP_OK;

    load_default_settings();

    ret = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &nvs);
    if (ESP_ERR_NVS_NOT_FOUND == ret) {
        return ESP_OK;
    }
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return ESP_OK;
    }

    ret = nvs_get_blob(nvs, SETTINGS_KEY, &persisted, &len);
    nvs_close(nvs);
    if (ESP_OK != ret || len != sizeof(persisted)) {
        ESP_LOGW(TAG, "settings blob missing or invalid, use defaults");
        return ESP_OK;
    }

    if (app_lock()) {
        s_settings.month_salary_yuan = persisted.month_salary_yuan;
        s_settings.sound_interval_yuan = persisted.sound_interval_yuan;
        s_settings.work_start_minutes = persisted.work_start_minutes;
        s_settings.work_end_minutes = persisted.work_end_minutes;
        if (!salary_settings_validate(&s_settings)) {
            s_settings.month_salary_yuan = DEFAULT_MONTH_SALARY_YUAN;
            s_settings.sound_interval_yuan = DEFAULT_SOUND_INTERVAL_YUAN;
            s_settings.work_start_minutes = DEFAULT_WORK_START_MINUTES;
            s_settings.work_end_minutes = DEFAULT_WORK_END_MINUTES;
        }
        app_unlock();
    }

    return ESP_OK;
}

static esp_err_t save_settings_to_nvs(const salary_settings_t *settings)
{
    persisted_settings_t persisted = {0};
    nvs_handle_t nvs = 0;
    esp_err_t ret = ESP_OK;

    if (!salary_settings_validate(settings)) {
        return ESP_ERR_INVALID_ARG;
    }

    persisted.month_salary_yuan = settings->month_salary_yuan;
    persisted.sound_interval_yuan = settings->sound_interval_yuan;
    persisted.work_start_minutes = settings->work_start_minutes;
    persisted.work_end_minutes = settings->work_end_minutes;

    ret = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ESP_OK != ret) {
        return ret;
    }

    ret = nvs_set_blob(nvs, SETTINGS_KEY, &persisted, sizeof(persisted));
    if (ESP_OK == ret) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return ret;
}

static esp_err_t sync_clock_from_rtc(void)
{
    aip8563_time_t rtc_time = {0};
    salary_clock_t clock = {0};
    esp_err_t ret = aip8563_init();

    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "RTC init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_rtc_power_lost = aip8563_power_lost();
    ret = aip8563_get_time(&rtc_time);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "RTC read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    clock = rtc_to_clock(&rtc_time);
    if (!salary_clock_validate(&clock)) {
        ESP_LOGW(TAG, "RTC returned an invalid date/time");
        return ESP_ERR_INVALID_RESPONSE;
    }
    set_clock_base(&clock);
    return ESP_OK;
}

static esp_err_t update_current_time(const salary_clock_t *new_clock)
{
    aip8563_time_t rtc_time = {0};
    esp_err_t ret = ESP_OK;

    if (!salary_clock_validate(new_clock)) {
        return ESP_ERR_INVALID_ARG;
    }

    rtc_time = clock_to_rtc(new_clock);
    ret = aip8563_set_time(&rtc_time);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "RTC write failed, keep software clock only: %s", esp_err_to_name(ret));
    } else {
        s_rtc_power_lost = false;
    }

    set_clock_base(new_clock);
    return ret;
}

static void build_settings_state(salary_ui_settings_state_t *state, const salary_clock_t *clock)
{
    salary_settings_t settings = {0};

    if (NULL == state || NULL == clock) {
        return;
    }

    memset(state, 0, sizeof(*state));
    if (app_lock()) {
        settings = s_settings;
        app_unlock();
    }

    snprintf(state->salary_text, sizeof(state->salary_text), "%lld", (long long)settings.month_salary_yuan);
    snprintf(state->sound_interval_text, sizeof(state->sound_interval_text), "%lld",
             (long long)settings.sound_interval_yuan);
    state->work_start_hour = (uint8_t)(settings.work_start_minutes / 60U);
    state->work_start_minute = (uint8_t)(settings.work_start_minutes % 60U);
    state->work_end_hour = (uint8_t)(settings.work_end_minutes / 60U);
    state->work_end_minute = (uint8_t)(settings.work_end_minutes % 60U);
    state->current_year = clock->year;
    state->current_month = clock->month;
    state->current_day = clock->day;
    state->current_hour = clock->hour;
    state->current_minute = clock->minute;
    state->current_second = clock->second;
}

static void build_money_state(const salary_settings_t *settings,
                              const salary_clock_t *clock,
                              const salary_calc_result_t *result,
                              salary_ui_money_state_t *ui_state)
{
    if (NULL == settings || NULL == clock || NULL == result || NULL == ui_state) {
        return;
    }

    memset(ui_state, 0, sizeof(*ui_state));
    salary_format_currency_cny(result->earned_cents, ui_state->amount_text, sizeof(ui_state->amount_text));
    snprintf(ui_state->progress_text, sizeof(ui_state->progress_text), "Today %u%%",
             (unsigned)result->progress_percent);
}

static void refresh_settings_page(bool clear_message)
{
    salary_clock_t current = get_current_clock();
    salary_ui_settings_state_t state = {0};
    char rtc_text[40] = {0};

    build_settings_state(&state, &current);
    salary_format_clock(&current, true, rtc_text, sizeof(rtc_text));
    salary_calculator_ui_apply_settings(&state);
    salary_calculator_ui_update_rtc_display(rtc_text, s_rtc_power_lost);
    if (clear_message) {
        salary_calculator_ui_set_message("", false);
    }
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0]) | ((uint16_t)data[1] << 8));
}

static bool parse_wav_file(FILE *fp, wav_info_t *info)
{
    uint8_t header[12] = {0};
    bool found_fmt = false;
    bool found_data = false;

    if (NULL == fp || NULL == info) {
        return false;
    }

    memset(info, 0, sizeof(*info));
    if (12 != fread(header, 1, sizeof(header), fp)) {
        return false;
    }
    if (0 != memcmp(header, "RIFF", 4) || 0 != memcmp(&header[8], "WAVE", 4)) {
        return false;
    }

    while (!found_data) {
        uint8_t chunk_header[8] = {0};
        uint32_t chunk_size = 0;
        long chunk_data_pos = 0;

        if (8 != fread(chunk_header, 1, sizeof(chunk_header), fp)) {
            break;
        }

        chunk_size = read_u32_le(&chunk_header[4]);
        chunk_data_pos = ftell(fp);

        if (0 == memcmp(chunk_header, "fmt ", 4)) {
            uint8_t fmt_data[16] = {0};

            if (chunk_size < sizeof(fmt_data) || sizeof(fmt_data) != fread(fmt_data, 1, sizeof(fmt_data), fp)) {
                return false;
            }
            info->audio_format = read_u16_le(&fmt_data[0]);
            info->channels = read_u16_le(&fmt_data[2]);
            info->sample_rate = read_u32_le(&fmt_data[4]);
            info->bits_per_sample = read_u16_le(&fmt_data[14]);
            found_fmt = true;
        } else if (0 == memcmp(chunk_header, "data", 4)) {
            info->data_offset = (uint32_t)chunk_data_pos;
            info->data_size = chunk_size;
            found_data = true;
        }

        if (0 != fseek(fp, chunk_data_pos + (long)chunk_size + (long)(chunk_size & 1U), SEEK_SET)) {
            return false;
        }
    }

    return found_fmt && found_data;
}

static esp_err_t play_coin_wav(void)
{
    FILE *fp = NULL;
    wav_info_t info = {0};
    uint8_t io_buf[512] = {0};

    if (!s_coin_wav_available || NULL == s_out_dev) {
        return ESP_ERR_NOT_FOUND;
    }

    fp = fopen(COIN_SOUND_FILE_PATH, "rb");
    if (NULL == fp) {
        return ESP_ERR_NOT_FOUND;
    }

    if (!parse_wav_file(fp, &info)) {
        fclose(fp);
        ESP_LOGW(TAG, "coin_burst.wav parse failed");
        return ESP_FAIL;
    }

    if (1U != info.audio_format || 1U != info.channels || 16000U != info.sample_rate || 16U != info.bits_per_sample) {
        fclose(fp);
        ESP_LOGW(TAG, "coin_burst.wav must be PCM mono 16-bit 16kHz");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (0 != fseek(fp, (long)info.data_offset, SEEK_SET)) {
        fclose(fp);
        return ESP_FAIL;
    }

    esp_codec_dev_set_out_vol(s_out_dev, AUDIO_OUTPUT_VOL);
    while (!feof(fp)) {
        size_t bytes_read = fread(io_buf, 1, sizeof(io_buf), fp);
        if (0 == bytes_read) {
            break;
        }
        if (ESP_OK != esp_codec_dev_write(s_out_dev, io_buf, bytes_read)) {
            fclose(fp);
            return ESP_FAIL;
        }
    }

    fclose(fp);
    return ESP_OK;
}

static esp_err_t init_audio_and_spiffs(void)
{
    esp_vfs_spiffs_conf_t spiffs_cfg = {
        .base_path = "/spiffs_data",
        .partition_label = "spiffs_data",
        .max_files = 4,
        .format_if_mount_failed = false,
    };
    struct stat file_info = {0};
    esp_err_t ret = board_laiwfs300_audio_init();

    if (ESP_OK != ret) {
        return ret;
    }

    s_out_dev = board_laiwfs300_audio_get_output_dev();
    if (NULL != s_out_dev) {
        esp_codec_dev_set_out_vol(s_out_dev, AUDIO_OUTPUT_VOL);
    }

    ret = esp_vfs_spiffs_register(&spiffs_cfg);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ESP_OK;
    }

    s_coin_wav_available = (0 == stat(COIN_SOUND_FILE_PATH, &file_info));
    if (!s_coin_wav_available) {
        ESP_LOGW(TAG, "coin_burst.wav not found, use tone fallback for now");
    } else {
        ESP_LOGI(TAG, "coin_burst.wav ready");
    }
    return ESP_OK;
}

static void play_coin_sound(void)
{
    if (ESP_OK == play_coin_wav()) {
        return;
    }

    board_laiwfs300_audio_play_tone(FALLBACK_TONE_HZ, FALLBACK_TONE_MS);
}

static void coin_sound_task(void *arg)
{
    (void)arg;

    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        play_coin_sound();
    }
}

static void request_coin_sound(void)
{
    if (NULL != s_audio_task_handle) {
        xTaskNotifyGive(s_audio_task_handle);
    }
}

static void on_settings_confirm(const salary_ui_form_t *form, void *user_ctx)
{
    salary_settings_t new_settings = {0};
    salary_clock_t current_clock = {0};
    salary_calc_result_t result = {0};
    salary_ui_money_state_t money_state = {0};
    salary_clock_t adjusted_clock = {0};
    int64_t month_salary_yuan = 0;
    int64_t sound_interval_yuan = 0;
    int64_t initial_bucket = 0;
    esp_err_t save_ret = ESP_OK;

    (void)user_ctx;

    if (NULL == form) {
        return;
    }

    if (!salary_parse_month_salary_yuan(form->salary_text, &month_salary_yuan)) {
        salary_calculator_ui_set_message("Monthly salary must be a positive integer.", true);
        return;
    }

    if (!salary_parse_month_salary_yuan(form->sound_interval_text, &sound_interval_yuan)) {
        salary_calculator_ui_set_message("Sound interval must be a positive integer.", true);
        return;
    }

    new_settings.month_salary_yuan = month_salary_yuan;
    new_settings.sound_interval_yuan = sound_interval_yuan;
    new_settings.work_start_minutes = (uint16_t)(form->work_start_hour * 60U + form->work_start_minute);
    new_settings.work_end_minutes = (uint16_t)(form->work_end_hour * 60U + form->work_end_minute);
    if (!salary_settings_validate(&new_settings)) {
        salary_calculator_ui_set_message("Check work start/end time. End must be later.", true);
        return;
    }

    if (form->current_time_dirty) {
        adjusted_clock.year = form->current_year;
        adjusted_clock.month = form->current_month;
        adjusted_clock.day = form->current_day;
        adjusted_clock.hour = form->current_hour;
        adjusted_clock.minute = form->current_minute;
        adjusted_clock.second = form->current_second;
        if (!salary_clock_validate(&adjusted_clock)) {
            salary_calculator_ui_set_message("RTC date/time is invalid.", true);
            return;
        }
        (void)update_current_time(&adjusted_clock);
    }

    save_ret = save_settings_to_nvs(&new_settings);
    if (ESP_OK != save_ret) {
        ESP_LOGW(TAG, "save settings failed: %s", esp_err_to_name(save_ret));
    }

    if (app_lock()) {
        s_settings = new_settings;
        app_unlock();
    }

    current_clock = get_current_clock();
    salary_calc_compute_result(&new_settings, &current_clock, &result);
    build_money_state(&new_settings, &current_clock, &result, &money_state);
    initial_bucket = result.earned_cents / (new_settings.sound_interval_yuan * 100LL);

    if (app_lock()) {
        s_last_sound_bucket = initial_bucket;
        s_money_page_active = true;
        app_unlock();
    }

    salary_calculator_ui_set_message("", false);
    salary_calculator_ui_update_money(&money_state);
    salary_calculator_ui_show_money_page();
}

static void on_back_to_settings(void *user_ctx)
{
    (void)user_ctx;

    if (app_lock()) {
        s_money_page_active = false;
        app_unlock();
    }

    refresh_settings_page(true);
    salary_calculator_ui_show_settings_page();
}

static void app_update_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)arg;

    while (true) {
        salary_clock_t current_clock = get_current_clock();
        salary_settings_t settings = {0};
        bool money_page_active = false;

        if (app_lock()) {
            settings = s_settings;
            money_page_active = s_money_page_active;
            app_unlock();
        }

        if (money_page_active) {
            salary_calc_result_t result = {0};
            salary_ui_money_state_t money_state = {0};
            int64_t new_bucket = 0;
            bool trigger_sound = false;

            salary_calc_compute_result(&settings, &current_clock, &result);
            build_money_state(&settings, &current_clock, &result, &money_state);
            salary_calculator_ui_update_money(&money_state);

            new_bucket = result.earned_cents / (settings.sound_interval_yuan * 100LL);
            if (app_lock()) {
                if (new_bucket > s_last_sound_bucket) {
                    s_last_sound_bucket = new_bucket;
                    trigger_sound = true;
                }
                app_unlock();
            }

            if (trigger_sound) {
                salary_calculator_ui_trigger_coin_burst();
                request_coin_sound();
            }
        }

        {
            char rtc_text[40] = {0};

            salary_format_clock(&current_clock, true, rtc_text, sizeof(rtc_text));
            salary_calculator_ui_update_rtc_display(rtc_text, s_rtc_power_lost);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_UPDATE_INTERVAL_MS));
    }
}

void app_main(void)
{
    salary_ui_callbacks_t callbacks = {
        .on_confirm = on_settings_confirm,
        .on_back = on_back_to_settings,
        .user_ctx = NULL,
    };
    BaseType_t task_ok = pdFAIL;
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Salary calculator demo starting");

    s_state_mutex = xSemaphoreCreateMutex();
    if (NULL == s_state_mutex) {
        ESP_LOGE(TAG, "failed to create state mutex");
        return;
    }

    ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = launcher_return_start_default();
    if (ESP_OK != ret && ESP_ERR_NOT_SUPPORTED != ret) {
        ESP_LOGW(TAG, "launcher return unavailable: %s", esp_err_to_name(ret));
    }

    ret = nvs_flash_init();
    if (ESP_ERR_NVS_NO_FREE_PAGES == ret || ESP_ERR_NVS_NEW_VERSION_FOUND == ret) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "nvs init failed: %s", esp_err_to_name(ret));
        return;
    }

    load_settings_from_nvs();
    ret = sync_clock_from_rtc();
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "using fallback software clock");
        set_clock_base(&s_clock_base);
    }

    ret = init_audio_and_spiffs();
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "audio init failed: %s", esp_err_to_name(ret));
    }

    ret = salary_calculator_ui_init(&callbacks);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "UI init failed: %s", esp_err_to_name(ret));
        return;
    }

    refresh_settings_page(true);
    task_ok = xTaskCreate(coin_sound_task, "salary_audio", AUDIO_TASK_STACK, NULL,
                          tskIDLE_PRIORITY + 2, &s_audio_task_handle);
    if (pdPASS != task_ok) {
        ESP_LOGW(TAG, "failed to create coin sound task; sound is disabled");
    }

    task_ok = xTaskCreate(app_update_task, "salary_tick", 6144, NULL, tskIDLE_PRIORITY + 2, NULL);
    if (pdPASS != task_ok) {
        ESP_LOGE(TAG, "failed to create update task");
        return;
    }

    ESP_LOGI(TAG, "Salary calculator demo ready");
}
