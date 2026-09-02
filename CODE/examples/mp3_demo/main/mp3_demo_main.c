#include "mp3_audio_output.h"
#include "mp3_catalog.h"
#include "mp3_catalog_scan.h"
#include "mp3_cover.h"
#include "mp3_file_io.h"
#include "mp3_lrc.h"
#include "mp3_player.h"
#include "mp3_spi_lock.h"
#include "mp3_ui.h"

#include "board_laiwfs300.h"
#include "board_pins.h"
#include "display_hal.h"
#include "storage_hal.h"

#include "esp_gmf_io_file.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "mp3_demo";

#define MP3_DEMO_VERSION "1.0.1"
#define MP3_TF_MOUNT_POINT "/tfcard"
#define MP3_LIBRARY_ROOT MP3_TF_MOUNT_POINT "/mp3"
#define MP3_TF_MAX_FREQ_KHZ 10000
#define MP3_TF_MAX_OPEN_FILES 8
#define MP3_TF_ALLOCATION_UNIT_SIZE (16U * 1024U)
#define MP3_SPI_TIMEOUT_MS 1000U
#define MP3_PLAYER_EVENT_QUEUE_LENGTH 16U
#define MP3_UI_COMMAND_QUEUE_LENGTH 8U
#define MP3_LOOP_DELAY_MS 10U
#define MP3_PROGRESS_REFRESH_MS 100U
#define MP3_HEALTH_INTERVAL_US (INT64_C(30) * INT64_C(1000000))

typedef struct {
    mp3_lrc_t lyrics;
    mp3_cover_t cover;
} mp3_song_resources_t;

typedef struct {
    mp3_spi_lock_t spi_lock;
    mp3_catalog_t catalog;
    mp3_song_t *songs;
    QueueHandle_t player_event_queue;
    QueueHandle_t ui_command_queue;
    portMUX_TYPE stats_lock;
    mp3_song_resources_t resources;
    mp3_player_snapshot_t latest_snapshot;
    uint32_t generation;
    uint32_t player_errors;
    uint32_t seek_count;
    uint32_t player_event_drops;
    uint32_t ui_command_drops;
    uint16_t current_index;
    uint16_t failure_attempts;
    int64_t next_health_us;
    int64_t next_progress_us;
    bool ui_ready;
    bool audio_ready;
    bool player_ready;
    bool amp_enabled;
} mp3_demo_app_t;

static mp3_demo_app_t s_app;

static void increment_saturated(uint32_t *value)
{
    if (NULL != value && UINT32_MAX > *value) {
        ++(*value);
    }
}

static storage_hal_config_t make_storage_config(void)
{
    const storage_hal_config_t config = {
        .mount_point = MP3_TF_MOUNT_POINT,
        .spi_host = SPI2_HOST,
        .cs_gpio_num = BOARD_LAIWFS300_GPIO_TF_SPI_CS,
        .max_freq_khz = MP3_TF_MAX_FREQ_KHZ,
        .max_files = MP3_TF_MAX_OPEN_FILES,
        .allocation_unit_size = MP3_TF_ALLOCATION_UNIT_SIZE,
        .format_if_mount_failed = false,
    };
    return config;
}

static void *lrc_psram_alloc(void *context, size_t size)
{
    (void)context;
    return heap_caps_malloc((0U == size) ? 1U : size,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void lrc_psram_free(void *context, void *memory)
{
    (void)context;
    heap_caps_free(memory);
}

static esp_err_t convert_lrc_result(mp3_lrc_result_t result)
{
    switch (result) {
    case MP3_LRC_OK:
        return ESP_OK;
    case MP3_LRC_INVALID_ARGUMENT:
        return ESP_ERR_INVALID_ARG;
    case MP3_LRC_FILE_TOO_LARGE:
        return ESP_ERR_INVALID_SIZE;
    case MP3_LRC_NO_MEMORY:
        return ESP_ERR_NO_MEM;
    default:
        return ESP_FAIL;
    }
}

static void release_song_resources(mp3_song_resources_t *resources)
{
    if (NULL == resources) {
        return;
    }
    mp3_cover_release(&resources->cover);
    mp3_lrc_release(&resources->lyrics);
}

static esp_err_t load_lyrics(mp3_demo_app_t *app, const mp3_song_t *song)
{
    uint8_t *raw = NULL;
    size_t raw_size = 0U;
    const mp3_lrc_allocator_t allocator = {
        .alloc = lrc_psram_alloc,
        .free = lrc_psram_free,
        .context = NULL,
    };

    if (NULL == app || NULL == song) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!song->has_lrc) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t result = mp3_file_read_all(
        song->lrc_path, &app->spi_lock, MP3_LRC_MAX_FILE_BYTES, false,
        &raw, &raw_size);
    if (ESP_OK == result) {
        result = convert_lrc_result(mp3_lrc_parse(
            raw, raw_size, &allocator, &app->resources.lyrics));
    }
    heap_caps_free(raw);
    if (ESP_OK == result) {
        ESP_LOGI(TAG,
                 "RESOURCE generation=%" PRIu32
                 " lrc=ok lines=%u malformed=%u truncated=%u",
                 app->generation,
                 (unsigned int)app->resources.lyrics.line_count,
                 (unsigned int)app->resources.lyrics.malformed_count,
                 (unsigned int)app->resources.lyrics.truncated_count);
    }
    return result;
}

static esp_err_t load_cover(mp3_demo_app_t *app, const mp3_song_t *song)
{
    if (NULL == app || NULL == song) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!song->has_cover) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t result = mp3_cover_load(
        song->cover_path, &app->spi_lock, MP3_UI_COVER_WIDTH,
        MP3_UI_COVER_HEIGHT, &app->resources.cover);
    if (ESP_OK == result) {
        ESP_LOGI(TAG,
                 "RESOURCE generation=%" PRIu32
                 " cover=ok source=%ux%u target=%ux%u",
                 app->generation,
                 (unsigned int)app->resources.cover.source_width,
                 (unsigned int)app->resources.cover.source_height,
                 (unsigned int)app->resources.cover.width,
                 (unsigned int)app->resources.cover.height);
    }
    return result;
}

static void load_optional_resources(mp3_demo_app_t *app,
                                    const mp3_song_t *song)
{
    if (NULL == app || NULL == song) {
        return;
    }
    esp_err_t result = load_lyrics(app, song);
    if (ESP_OK != result && ESP_ERR_NOT_FOUND != result) {
        ESP_LOGW(TAG,
                 "RESOURCE generation=%" PRIu32 " lrc=degraded error=%s",
                 app->generation, esp_err_to_name(result));
        mp3_lrc_release(&app->resources.lyrics);
    }
    result = load_cover(app, song);
    if (ESP_OK != result && ESP_ERR_NOT_FOUND != result) {
        ESP_LOGW(TAG,
                 "RESOURCE generation=%" PRIu32
                 " cover=degraded error=%s path=%s requirement="
                 "baseline_jpeg_max_512px_512KiB",
                 app->generation, esp_err_to_name(result), song->cover_path);
        mp3_cover_release(&app->resources.cover);
    }
}

static void set_amp(mp3_demo_app_t *app, bool enabled)
{
    if (NULL == app || !app->audio_ready || app->amp_enabled == enabled) {
        return;
    }
    esp_err_t result = mp3_audio_output_set_amp(enabled);
    if (ESP_OK == result) {
        app->amp_enabled = enabled;
    } else {
        ESP_LOGE(TAG, "AMP enabled=%d error=%s", enabled,
                 esp_err_to_name(result));
    }
}

static void player_event_received(const mp3_player_event_t *event,
                                  void *context)
{
    mp3_demo_app_t *app = context;

    if (NULL == event || NULL == app || NULL == app->player_event_queue) {
        return;
    }
    if (pdTRUE != xQueueSend(app->player_event_queue, event, 0)) {
        portENTER_CRITICAL(&app->stats_lock);
        increment_saturated(&app->player_event_drops);
        portEXIT_CRITICAL(&app->stats_lock);
    }
}

static void ui_command_received(const mp3_ui_command_t *command,
                                void *context)
{
    mp3_demo_app_t *app = context;

    if (NULL == command || NULL == app || NULL == app->ui_command_queue) {
        return;
    }
    if (pdTRUE != xQueueSend(app->ui_command_queue, command, 0)) {
        portENTER_CRITICAL(&app->stats_lock);
        increment_saturated(&app->ui_command_drops);
        portEXIT_CRITICAL(&app->stats_lock);
    }
}

static const char *state_name(mp3_player_state_t state)
{
    switch (state) {
    case MP3_PLAYER_STATE_EMPTY:
        return "EMPTY";
    case MP3_PLAYER_STATE_LOADING:
        return "LOADING";
    case MP3_PLAYER_STATE_PLAYING:
        return "PLAYING";
    case MP3_PLAYER_STATE_PAUSED:
        return "PAUSED";
    case MP3_PLAYER_STATE_SEEKING:
        return "SEEKING";
    case MP3_PLAYER_STATE_STOPPED:
        return "STOPPED";
    case MP3_PLAYER_STATE_ERROR:
        return "ERROR";
    default:
        return "INVALID";
    }
}

static uint16_t next_index(const mp3_demo_app_t *app, uint16_t index)
{
    if (NULL == app || 0U == app->catalog.count) {
        return 0U;
    }
    return (uint16_t)((index + 1U) % app->catalog.count);
}

static uint16_t previous_index(const mp3_demo_app_t *app, uint16_t index)
{
    if (NULL == app || 0U == app->catalog.count) {
        return 0U;
    }
    return (0U == index) ? (uint16_t)(app->catalog.count - 1U)
                         : (uint16_t)(index - 1U);
}

static esp_err_t try_start_song(mp3_demo_app_t *app, uint16_t index,
                                const char *reason)
{
    if (NULL == app || NULL == reason || !app->player_ready ||
        index >= app->catalog.count) {
        return ESP_ERR_INVALID_ARG;
    }
    ++app->generation;
    if (0U == app->generation) {
        ++app->generation;
    }
    app->current_index = index;
    const mp3_song_t *song = &app->songs[index];

    if (app->ui_ready) {
        mp3_ui_clear_song(app->generation, index, song->title);
        mp3_ui_process();
    }
    (void)mp3_player_stop();
    release_song_resources(&app->resources);

    ESP_LOGI(TAG,
             "SONG generation=%" PRIu32
             " index=%u count=%u reason=%s title=%s",
             app->generation, (unsigned int)index,
             (unsigned int)app->catalog.count, reason, song->title);
    load_optional_resources(app, song);
    if (app->ui_ready) {
        mp3_ui_show_song(app->generation, index, song->title,
                         &app->resources.lyrics, &app->resources.cover);
    }

    esp_err_t result = mp3_audio_output_set_amp(true);
    if (ESP_OK == result) {
        app->amp_enabled = true;
        result = mp3_player_play(song->audio_path, index, app->generation);
    }
    if (ESP_OK != result) {
        ESP_LOGE(TAG,
                 "PLAYER start_failed generation=%" PRIu32
                 " index=%u error=%s",
                 app->generation, (unsigned int)index,
                 esp_err_to_name(result));
    }
    return result;
}

static void enter_all_songs_failed(mp3_demo_app_t *app)
{
    if (NULL == app) {
        return;
    }
    set_amp(app, false);
    ESP_LOGE(TAG, "PLAYER all_songs_failed attempts=%u count=%u",
             (unsigned int)app->failure_attempts,
             (unsigned int)app->catalog.count);
    if (app->ui_ready) {
        mp3_ui_show_status("MP3", "无可播放歌曲", true);
    }
}

static esp_err_t select_song(mp3_demo_app_t *app, uint16_t start_index,
                             const char *reason, bool reset_failure_budget)
{
    if (NULL == app || NULL == reason || 0U == app->catalog.count ||
        start_index >= app->catalog.count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (reset_failure_budget) {
        app->failure_attempts = 0U;
    }

    uint16_t index = start_index;
    while (app->failure_attempts < app->catalog.count) {
        esp_err_t result = try_start_song(app, index, reason);
        if (ESP_OK == result) {
            return ESP_OK;
        }
        increment_saturated(&app->player_errors);
        ++app->failure_attempts;
        ESP_LOGW(TAG,
                 "PLAYER auto_skip attempt=%u count=%u failed_index=%u",
                 (unsigned int)app->failure_attempts,
                 (unsigned int)app->catalog.count, (unsigned int)index);
        index = next_index(app, index);
        reason = "start_failed";
    }
    enter_all_songs_failed(app);
    return ESP_FAIL;
}

static void refresh_snapshot(mp3_demo_app_t *app)
{
    if (NULL == app || !app->player_ready) {
        return;
    }
    mp3_player_snapshot_t snapshot = {0};
    if (mp3_player_get_snapshot(&snapshot)) {
        app->latest_snapshot = snapshot;
        if (app->ui_ready) {
            mp3_ui_update_snapshot(&snapshot);
        }
    }
}

static void handle_player_error(mp3_demo_app_t *app,
                                const mp3_player_event_t *event)
{
    increment_saturated(&app->player_errors);
    ++app->failure_attempts;
    ESP_LOGE(TAG,
             "PLAYER error generation=%" PRIu32
             " index=%u source=%" PRId32 " attempt=%u/%u",
             event->generation, (unsigned int)event->song_index,
             event->error_source, (unsigned int)app->failure_attempts,
             (unsigned int)app->catalog.count);
    if (app->failure_attempts >= app->catalog.count) {
        enter_all_songs_failed(app);
        return;
    }
    (void)select_song(app, next_index(app, app->current_index),
                      "error_skip", false);
}

static void handle_player_event(mp3_demo_app_t *app,
                                const mp3_player_event_t *event)
{
    if (NULL == app || NULL == event ||
        event->generation != app->generation) {
        return;
    }
    switch (event->type) {
    case MP3_PLAYER_EVENT_PLAYED:
        ESP_LOGI(TAG, "PLAYER played generation=%" PRIu32 " index=%u",
                 event->generation, (unsigned int)event->song_index);
        break;
    case MP3_PLAYER_EVENT_PAUSED:
        ESP_LOGI(TAG, "PLAYER paused generation=%" PRIu32,
                 event->generation);
        break;
    case MP3_PLAYER_EVENT_SEEK_DONE:
        ESP_LOGI(TAG, "PLAYER seek_done generation=%" PRIu32,
                 event->generation);
        if (app->ui_ready) {
            mp3_ui_seek_completed(event->generation, ESP_OK);
        }
        break;
    case MP3_PLAYER_EVENT_FINISHED:
        ESP_LOGI(TAG,
                 "PLAYER natural_end generation=%" PRIu32 " index=%u",
                 event->generation, (unsigned int)event->song_index);
        app->failure_attempts = 0U;
        (void)select_song(app, next_index(app, app->current_index),
                          "finished", false);
        break;
    case MP3_PLAYER_EVENT_ERROR:
        handle_player_error(app, event);
        break;
    case MP3_PLAYER_EVENT_STOPPED:
    default:
        break;
    }
    refresh_snapshot(app);
}

static void handle_seek_command(mp3_demo_app_t *app,
                                const mp3_seek_request_t *request)
{
    if (NULL == app || NULL == request) {
        return;
    }
    uint64_t from_ms = app->latest_snapshot.position_ms;
    const char *source_state = state_name(app->latest_snapshot.state);
    esp_err_t result = mp3_player_seek(request->generation,
                                       request->target_ms);
    if (ESP_OK == result) {
        increment_saturated(&app->seek_count);
    } else if (app->ui_ready) {
        mp3_ui_seek_completed(request->generation, result);
    }
    ESP_LOGI(TAG,
             "SEEK generation=%" PRIu32 " from_ms=%" PRIu64
             " target_ms=%" PRIu64 " state=%s keep_paused=%d result=%s",
             request->generation, from_ms, request->target_ms, source_state,
             request->keep_paused, esp_err_to_name(result));
}

static void handle_ui_command(mp3_demo_app_t *app,
                              const mp3_ui_command_t *command)
{
    if (NULL == app || NULL == command) {
        return;
    }
    if (MP3_UI_COMMAND_SEEK == command->type) {
        if (app->player_ready) {
            handle_seek_command(app, &command->seek);
        }
        return;
    }
    if (!app->player_ready || 0U == app->catalog.count) {
        return;
    }

    switch (command->type) {
    case MP3_UI_COMMAND_PREVIOUS:
        (void)select_song(app, previous_index(app, app->current_index),
                          "user_previous", true);
        break;
    case MP3_UI_COMMAND_TOGGLE_PLAYBACK:
        if (MP3_PLAYER_STATE_PAUSED == app->latest_snapshot.state) {
            esp_err_t result = mp3_player_resume();
            ESP_LOGI(TAG, "CONTROL resume result=%s",
                     esp_err_to_name(result));
        } else if (MP3_PLAYER_STATE_PLAYING == app->latest_snapshot.state) {
            esp_err_t result = mp3_player_pause();
            ESP_LOGI(TAG, "CONTROL pause result=%s",
                     esp_err_to_name(result));
        } else if (MP3_PLAYER_STATE_STOPPED == app->latest_snapshot.state ||
                   MP3_PLAYER_STATE_ERROR == app->latest_snapshot.state) {
            (void)select_song(app, app->current_index, "user_replay", true);
        }
        break;
    case MP3_UI_COMMAND_NEXT:
        (void)select_song(app, next_index(app, app->current_index),
                          "user_next", true);
        break;
    case MP3_UI_COMMAND_SELECT_SONG:
        if (command->song_index < app->catalog.count) {
            (void)select_song(app, command->song_index, "user_select", true);
        }
        break;
    case MP3_UI_COMMAND_SEEK:
    default:
        break;
    }
    refresh_snapshot(app);
}

static void log_health(mp3_demo_app_t *app)
{
    mp3_spi_lock_stats_t spi_stats = {0};
    uint32_t player_event_drops = 0U;
    uint32_t ui_command_drops = 0U;

    if (NULL == app) {
        return;
    }
    refresh_snapshot(app);
    mp3_spi_lock_get_stats(&app->spi_lock, &spi_stats);
    portENTER_CRITICAL(&app->stats_lock);
    player_event_drops = app->player_event_drops;
    ui_command_drops = app->ui_command_drops;
    portEXIT_CRITICAL(&app->stats_lock);
    unsigned int displayed_song =
        (0U == app->catalog.count) ? 0U : (unsigned int)app->current_index + 1U;
    ESP_LOGI(TAG,
             "HEALTH state=%s song=%u/%u position=%" PRIu64
             "/%" PRIu64 " underrun=0 player_err=%" PRIu32
             " seek=%" PRIu32 " lcd_err=%" PRIu32
             " touch_err=%" PRIu32 " spi_wait_max=%" PRIu64
             " spi_timeout=%" PRIu32 " spi_balance=%" PRIu32
             "/%" PRIu32 " event_drop=%" PRIu32 "/%" PRIu32
             " heap_internal=%u psram_free=%u",
             state_name(app->latest_snapshot.state), displayed_song,
             (unsigned int)app->catalog.count,
             app->latest_snapshot.position_ms,
             app->latest_snapshot.duration_ms, app->player_errors,
             app->seek_count, mp3_ui_get_lcd_error_count(),
             mp3_ui_get_touch_error_count(), spi_stats.max_wait_us,
             spi_stats.timeout_count, spi_stats.acquire_count,
             spi_stats.release_count, player_event_drops, ui_command_drops,
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static esp_err_t initialize_ui(mp3_demo_app_t *app, bool touch_available)
{
    if (NULL == app) {
        return ESP_ERR_INVALID_ARG;
    }
    app->player_event_queue = xQueueCreate(
        MP3_PLAYER_EVENT_QUEUE_LENGTH, sizeof(mp3_player_event_t));
    app->ui_command_queue = xQueueCreate(
        MP3_UI_COMMAND_QUEUE_LENGTH, sizeof(mp3_ui_command_t));
    if (NULL == app->player_event_queue || NULL == app->ui_command_queue) {
        return ESP_ERR_NO_MEM;
    }
    const mp3_ui_config_t config = {
        .songs = app->songs,
        .song_count = 0U,
        .spi_lock = &app->spi_lock,
        .command_callback = ui_command_received,
        .command_context = app,
        .touch_available = touch_available,
    };
    esp_err_t result = mp3_ui_init(&config);
    app->ui_ready = (ESP_OK == result);
    return result;
}

static esp_err_t mount_and_scan_library(mp3_demo_app_t *app)
{
    if (NULL == app) {
        return ESP_ERR_INVALID_ARG;
    }
    const storage_hal_config_t storage_config = make_storage_config();
    if (!mp3_spi_lock_acquire(&app->spi_lock, MP3_SPI_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = storage_hal_init(&storage_config);
    mp3_spi_lock_release(&app->spi_lock);
    if (ESP_OK != result) {
        return result;
    }
    ESP_LOGI(TAG, "INIT tf=ok speed_khz=%d", MP3_TF_MAX_FREQ_KHZ);

    result = mp3_catalog_scan(MP3_LIBRARY_ROOT, &app->spi_lock,
                              &app->catalog);
    ESP_LOGI(TAG,
             "SCAN result=%s found=%u accepted=%u rejected=%u truncated=%u",
             esp_err_to_name(result),
             (unsigned int)app->catalog.stats.found_count,
             (unsigned int)app->catalog.count,
             (unsigned int)app->catalog.stats.rejected_count,
             (unsigned int)app->catalog.stats.truncated_count);
    if (ESP_OK == result) {
        result = mp3_ui_set_catalog(app->songs, app->catalog.count);
    }
    return result;
}

static void run_application_loop(mp3_demo_app_t *app)
{
    if (NULL == app || !app->ui_ready) {
        return;
    }
    int64_t now_us = esp_timer_get_time();
    app->next_health_us = now_us + MP3_HEALTH_INTERVAL_US;
    app->next_progress_us =
        now_us + ((int64_t)MP3_PROGRESS_REFRESH_MS * 1000);

    while (true) {
        mp3_player_event_t player_event = {0};
        mp3_ui_command_t ui_command = {0};

        while (pdTRUE == xQueueReceive(app->player_event_queue,
                                       &player_event, 0)) {
            handle_player_event(app, &player_event);
        }
        while (pdTRUE == xQueueReceive(app->ui_command_queue,
                                       &ui_command, 0)) {
            handle_ui_command(app, &ui_command);
        }
        now_us = esp_timer_get_time();
        if (app->player_ready && now_us >= app->next_progress_us) {
            refresh_snapshot(app);
            app->next_progress_us =
                now_us + ((int64_t)MP3_PROGRESS_REFRESH_MS * 1000);
        }
        mp3_ui_process();
        if (now_us >= app->next_health_us) {
            log_health(app);
            app->next_health_us = now_us + MP3_HEALTH_INTERVAL_US;
        }
        vTaskDelay(pdMS_TO_TICKS(MP3_LOOP_DELAY_MS));
    }
}

void app_main(void)
{
    memset(&s_app, 0, sizeof(s_app));
    s_app.stats_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    ESP_LOGI(TAG,
             "INIT version=%s lcd_hz=%u buffers=2x%ux%u tf_khz=%d",
             MP3_DEMO_VERSION, (unsigned int)MP3_UI_LCD_PIXEL_CLOCK_HZ,
             (unsigned int)BOARD_LAIWFS300_LCD_V_RES,
             (unsigned int)MP3_UI_LCD_BUFFER_LINES, MP3_TF_MAX_FREQ_KHZ);

    esp_err_t result = nvs_flash_init();
    if (ESP_OK != result) {
        ESP_LOGW(TAG, "INIT nvs=degraded error=%s", esp_err_to_name(result));
    } else {
        ESP_LOGI(TAG, "INIT nvs=ok");
    }
    result = board_laiwfs300_init();
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "INIT board=fail error=%s", esp_err_to_name(result));
        return;
    }
    ESP_LOGI(TAG, "INIT board=ok");

    if (!mp3_spi_lock_init(&s_app.spi_lock)) {
        ESP_LOGE(TAG, "INIT spi_lock=fail");
        return;
    }
    esp_gmf_io_file_set_lock_callback(mp3_spi_lock_gmf_callback,
                                      &s_app.spi_lock);

    result = board_laiwfs300_display_init_with_config(
        MP3_UI_LCD_PIXEL_CLOCK_HZ, MP3_UI_LCD_BUFFER_LINES);
    if (ESP_OK == result) {
        result = display_hal_set_orientation(true, false, true);
    }
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "INIT lcd=fail error=%s", esp_err_to_name(result));
        return;
    }
    ESP_LOGI(TAG, "INIT lcd=ok wait_per_flush=1");

    result = board_laiwfs300_touch_init();
    bool touch_available = (ESP_OK == result);
    if (touch_available) {
        ESP_LOGI(TAG, "INIT touch=ok");
    } else {
        ESP_LOGW(TAG, "INIT touch=degraded error=%s",
                 esp_err_to_name(result));
    }

    s_app.songs = heap_caps_calloc(MP3_CATALOG_MAX_SONGS,
                                   sizeof(*s_app.songs),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == s_app.songs ||
        !mp3_catalog_init(&s_app.catalog, s_app.songs,
                          MP3_CATALOG_MAX_SONGS)) {
        ESP_LOGE(TAG, "INIT catalog_memory=fail");
        return;
    }
    result = initialize_ui(&s_app, touch_available);
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "INIT ui=fail error=%s", esp_err_to_name(result));
        return;
    }
    ESP_LOGI(TAG, "INIT ui=ok font=ok touch=%s",
             touch_available ? "ready" : "disabled");
    mp3_ui_process();

    result = mount_and_scan_library(&s_app);
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "INIT library=fail error=%s", esp_err_to_name(result));
        mp3_ui_show_status("MP3", "TF卡或曲库读取失败", true);
        run_application_loop(&s_app);
        return;
    }
    if (0U == s_app.catalog.count) {
        ESP_LOGW(TAG, "INIT catalog=empty");
        mp3_ui_show_status("MP3", "曲库为空", false);
        run_application_loop(&s_app);
        return;
    }

    esp_audio_render_stream_handle_t render_stream = NULL;
    result = mp3_audio_output_init(&render_stream);
    s_app.audio_ready = (ESP_OK == result);
    if (ESP_OK == result) {
        result = mp3_player_init(render_stream, player_event_received, &s_app);
    }
    s_app.player_ready = (ESP_OK == result);
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "INIT player=fail error=%s", esp_err_to_name(result));
        mp3_ui_show_status("MP3", "音频初始化失败", true);
        run_application_loop(&s_app);
        return;
    }
    ESP_LOGI(TAG, "INIT player=ok");
    (void)select_song(&s_app, 0U, "startup", true);
    refresh_snapshot(&s_app);
    run_application_loop(&s_app);
}
