#include "game_2048_core.h"
#include "game_2048_storage.h"
#include "game_2048_ui.h"

#include "board_laiwfs300.h"
#include "board_pins.h"
#include "display_hal.h"
#include "launcher_return.h"
#include "touch_hal.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "touch_2048";

#define TOUCH_2048_VERSION "1.0.0"
#if defined(CONFIG_TOUCH_2048_LCD_PIXEL_CLOCK_20MHZ)
#define TOUCH_2048_LCD_PIXEL_CLOCK_HZ 20000000U
#else
#define TOUCH_2048_LCD_PIXEL_CLOCK_HZ 40000000U
#endif
#define TOUCH_2048_LCD_BUFFER_LINES 80U
#define TOUCH_2048_LCD_WAIT_MS 1000
#define TOUCH_2048_LVGL_HOR_RES BOARD_LAIWFS300_LCD_V_RES
#define TOUCH_2048_LVGL_VER_RES BOARD_LAIWFS300_LCD_H_RES
#define TOUCH_2048_LVGL_TICK_MS 2U
#define TOUCH_2048_LVGL_LOOP_DELAY_MS 10U
#define TOUCH_2048_HEALTH_INTERVAL_US (INT64_C(30) * INT64_C(1000000))
#define TOUCH_2048_TOUCH_MAX_POINTS 2U
#define TOUCH_2048_LCD_ERROR_LOG_INTERVAL 100U
#define TOUCH_2048_NVS_NAMESPACE "game2048"
#define TOUCH_2048_NVS_BEST_SCORE_KEY "best_score"

typedef struct {
    nvs_handle_t handle;
    esp_err_t last_error;
    game_2048_storage_read_result_t last_read_result;
    bool ready;
} game_2048_nvs_context_t;

typedef struct {
    game_2048_board_t board;
    game_2048_snapshot_t undo_snapshot;
    game_2048_storage_backend_t storage;
    game_2048_nvs_context_t nvs;
    uint32_t best_score;
    uint32_t persisted_best_score;
    uint32_t valid_moves;
    uint32_t invalid_moves;
    uint32_t undo_count;
    uint32_t restart_count;
    uint32_t touch_rejections;
    uint32_t touch_read_errors;
    uint32_t game_errors;
    uint32_t nvs_errors;
    uint32_t lcd_errors;
    int64_t next_health_log_us;
    bool touch_available;
    bool win_announced;
} game_2048_app_t;

static game_2048_app_t s_app;
static esp_timer_handle_t s_lvgl_tick_timer;

static void increment_counter(uint32_t *counter)
{
    if ((NULL != counter) && (UINT32_MAX > *counter)) {
        ++(*counter);
    }
}

static uint32_t maximum_tile_value(const game_2048_board_t *board)
{
    const uint8_t exponent = game_2048_max_exponent(board);
    return (0U == exponent) ? 0U : (UINT32_C(1) << exponent);
}

static const char *direction_name(game_2048_direction_t direction)
{
    switch (direction) {
    case GAME_2048_DIRECTION_UP:
        return "UP";
    case GAME_2048_DIRECTION_DOWN:
        return "DOWN";
    case GAME_2048_DIRECTION_LEFT:
        return "LEFT";
    case GAME_2048_DIRECTION_RIGHT:
        return "RIGHT";
    default:
        return "INVALID";
    }
}

static const char *state_name(game_2048_state_t state)
{
    switch (state) {
    case GAME_2048_STATE_PLAYING:
        return "PLAYING";
    case GAME_2048_STATE_WON:
        return "WON";
    case GAME_2048_STATE_LOST:
        return "LOST";
    default:
        return "INVALID";
    }
}

static uint32_t hardware_rng(void *ctx)
{
    (void)ctx;
    return esp_random();
}

static game_2048_storage_read_result_t nvs_read_best_score(void *ctx,
                                                            uint32_t *score)
{
    game_2048_nvs_context_t *nvs_ctx = ctx;
    if ((NULL == nvs_ctx) || (NULL == score) || !nvs_ctx->ready) {
        return GAME_2048_STORAGE_READ_ERROR;
    }

    const esp_err_t result = nvs_get_u32(nvs_ctx->handle,
                                         TOUCH_2048_NVS_BEST_SCORE_KEY,
                                         score);
    nvs_ctx->last_error = result;
    if (ESP_OK == result) {
        nvs_ctx->last_read_result = GAME_2048_STORAGE_READ_OK;
    } else if (ESP_ERR_NVS_NOT_FOUND == result) {
        nvs_ctx->last_read_result = GAME_2048_STORAGE_READ_NOT_FOUND;
    } else if (ESP_ERR_NVS_TYPE_MISMATCH == result) {
        nvs_ctx->last_read_result = GAME_2048_STORAGE_READ_INVALID;
    } else {
        nvs_ctx->last_read_result = GAME_2048_STORAGE_READ_ERROR;
    }
    return nvs_ctx->last_read_result;
}

static bool nvs_write_best_score(void *ctx, uint32_t score)
{
    game_2048_nvs_context_t *nvs_ctx = ctx;
    if ((NULL == nvs_ctx) || !nvs_ctx->ready) {
        return false;
    }

    esp_err_t result = nvs_set_u32(nvs_ctx->handle,
                                   TOUCH_2048_NVS_BEST_SCORE_KEY,
                                   score);
    if (ESP_OK == result) {
        result = nvs_commit(nvs_ctx->handle);
    }
    nvs_ctx->last_error = result;
    return ESP_OK == result;
}

static void initialize_storage(game_2048_app_t *app)
{
    if (NULL == app) {
        return;
    }

    app->storage.read_best_score = nvs_read_best_score;
    app->storage.write_best_score = nvs_write_best_score;
    app->storage.ctx = &app->nvs;
    app->nvs.last_read_result = GAME_2048_STORAGE_READ_ERROR;

    esp_err_t result = nvs_flash_init();
    if (ESP_OK != result) {
        app->nvs.last_error = result;
        increment_counter(&app->nvs_errors);
        ESP_LOGW(TAG,
                 "[2048][nvs_error] op=init error=%s persistence=disabled",
                 esp_err_to_name(result));
        return;
    }

    result = nvs_open(TOUCH_2048_NVS_NAMESPACE, NVS_READWRITE,
                      &app->nvs.handle);
    if (ESP_OK != result) {
        app->nvs.last_error = result;
        increment_counter(&app->nvs_errors);
        ESP_LOGW(TAG,
                 "[2048][nvs_error] op=open error=%s persistence=disabled",
                 esp_err_to_name(result));
        return;
    }
    app->nvs.ready = true;

    uint32_t stored_score = 0U;
    const game_2048_storage_load_result_t load_result =
        game_2048_storage_load_best_score(&app->storage, &stored_score);
    if (GAME_2048_STORAGE_LOAD_OK == load_result) {
        app->best_score = stored_score;
        app->persisted_best_score = stored_score;
        ESP_LOGI(TAG, "[2048][startup] best_score=%" PRIu32 " source=nvs",
                 stored_score);
        return;
    }

    app->best_score = 0U;
    app->persisted_best_score = 0U;
    if (GAME_2048_STORAGE_READ_NOT_FOUND == app->nvs.last_read_result) {
        ESP_LOGI(TAG, "[2048][startup] best_score=0 source=default");
        return;
    }

    increment_counter(&app->nvs_errors);
    ESP_LOGW(TAG,
             "[2048][nvs_error] op=read error=%s fallback=0",
             esp_err_to_name(app->nvs.last_error));
}

static void persist_best_score(game_2048_app_t *app)
{
    if ((NULL == app) || !app->nvs.ready ||
        (app->persisted_best_score == app->best_score)) {
        return;
    }
    if (game_2048_storage_save_best_score(&app->storage, app->best_score)) {
        app->persisted_best_score = app->best_score;
        return;
    }

    increment_counter(&app->nvs_errors);
    ESP_LOGW(TAG,
             "[2048][nvs_error] op=write score=%" PRIu32 " error=%s",
             app->best_score, esp_err_to_name(app->nvs.last_error));
}

static void render_game(const game_2048_app_t *app)
{
    if (NULL == app) {
        return;
    }
    game_2048_ui_render(&app->board, app->best_score,
                        app->undo_snapshot.valid, app->touch_available);
}

static void log_move(const game_2048_app_t *app,
                     game_2048_direction_t direction,
                     bool changed)
{
    if (NULL == app) {
        return;
    }
    ESP_LOGI(TAG,
             "[2048][move] dir=%s changed=%u score=%" PRIu32
             " max=%" PRIu32 " empty=%u state=%s",
             direction_name(direction), changed ? 1U : 0U,
             app->board.score, maximum_tile_value(&app->board),
             (unsigned int)game_2048_empty_count(&app->board),
             state_name(app->board.state));
}

static void direction_requested(void *ctx, game_2048_direction_t direction)
{
    game_2048_app_t *app = ctx;
    if (NULL == app) {
        return;
    }

    const game_2048_board_t before = app->board;
    const game_2048_move_result_t result =
        game_2048_move(&app->board, direction, hardware_rng, NULL);
    if (GAME_2048_MOVE_ERROR == result) {
        increment_counter(&app->invalid_moves);
        increment_counter(&app->game_errors);
        ESP_LOGE(TAG, "[2048][error] op=move dir=%s", direction_name(direction));
        render_game(app);
        return;
    }

    if (GAME_2048_MOVE_CHANGED == result) {
        increment_counter(&app->valid_moves);
        if (!game_2048_snapshot_save(&app->undo_snapshot, &before)) {
            increment_counter(&app->game_errors);
            ESP_LOGE(TAG, "[2048][error] op=snapshot_save");
        }
        if (app->board.score > app->best_score) {
            app->best_score = app->board.score;
            persist_best_score(app);
        }
    } else {
        increment_counter(&app->invalid_moves);
    }

    log_move(app, direction, GAME_2048_MOVE_CHANGED == result);
    if (!app->win_announced && !before.win_reported &&
        app->board.win_reported) {
        app->win_announced = true;
        ESP_LOGI(TAG, "[2048][state] event=won score=%" PRIu32,
                 app->board.score);
        game_2048_ui_show_win_notice();
    }
    if ((GAME_2048_STATE_LOST != before.state) &&
        (GAME_2048_STATE_LOST == app->board.state)) {
        ESP_LOGI(TAG, "[2048][state] event=lost score=%" PRIu32,
                 app->board.score);
    }
    render_game(app);
}

static void undo_requested(void *ctx)
{
    game_2048_app_t *app = ctx;
    if ((NULL == app) ||
        !game_2048_snapshot_restore(&app->undo_snapshot, &app->board)) {
        return;
    }

    increment_counter(&app->undo_count);
    ESP_LOGI(TAG,
             "[2048][undo] score=%" PRIu32 " max=%" PRIu32
             " empty=%u state=%s",
             app->board.score, maximum_tile_value(&app->board),
             (unsigned int)game_2048_empty_count(&app->board),
             state_name(app->board.state));
    render_game(app);
}

static void restart_requested(void *ctx)
{
    game_2048_app_t *app = ctx;
    if (NULL == app) {
        return;
    }

    game_2048_board_t next = app->board;
    if (!game_2048_new_game(&next, hardware_rng, NULL)) {
        increment_counter(&app->game_errors);
        ESP_LOGE(TAG, "[2048][error] op=restart");
        return;
    }

    app->board = next;
    game_2048_snapshot_clear(&app->undo_snapshot);
    app->win_announced = false;
    increment_counter(&app->restart_count);
    ESP_LOGI(TAG, "[2048][restart] best=%" PRIu32, app->best_score);
    render_game(app);
}

static void touch_rejected(void *ctx)
{
    game_2048_app_t *app = ctx;
    if (NULL != app) {
        increment_counter(&app->touch_rejections);
    }
}

static void lvgl_flush_cb(lv_disp_drv_t *driver,
                          const lv_area_t *area,
                          lv_color_t *color_map)
{
    const uint32_t pixel_count =
        (uint32_t)(area->x2 - area->x1 + 1) *
        (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t *pixels = (uint16_t *)color_map;
    for (uint32_t index = 0U; index < pixel_count; ++index) {
        const uint16_t pixel = pixels[index];
        const uint16_t red = (pixel >> 11) & 0x1FU;
        const uint16_t green = (pixel >> 5) & 0x3FU;
        const uint16_t blue = pixel & 0x1FU;
        const uint16_t bgr = (uint16_t)((blue << 11) | (green << 5) | red);
        pixels[index] = (uint16_t)((bgr >> 8) | (bgr << 8));
    }

    esp_err_t result = display_hal_draw_bitmap_rgb565(
        area->x1, area->y1,
        area->x2 - area->x1 + 1,
        area->y2 - area->y1 + 1,
        pixels);
    if (ESP_OK == result) {
        result = display_hal_wait_pending(TOUCH_2048_LCD_WAIT_MS);
    }
    if (ESP_OK != result) {
        increment_counter(&s_app.lcd_errors);
        if ((1U == s_app.lcd_errors) ||
            (0U == (s_app.lcd_errors % TOUCH_2048_LCD_ERROR_LOG_INTERVAL))) {
            ESP_LOGE(TAG,
                     "[2048][lcd_error] count=%" PRIu32 " error=%s",
                     s_app.lcd_errors, esp_err_to_name(result));
        }
    }
    lv_disp_flush_ready(driver);
}

static lv_coord_t clamp_coordinate(int32_t value, int32_t maximum)
{
    if (0 > value) {
        return 0;
    }
    if (maximum < value) {
        return (lv_coord_t)maximum;
    }
    return (lv_coord_t)value;
}

static void lvgl_touch_read_cb(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    static lv_coord_t last_x;
    static lv_coord_t last_y;

    touch_panel_point_t point = {0};
    uint8_t touch_count = 0U;
    const esp_err_t result = touch_panel_read_point(&point, &touch_count);
    if ((ESP_OK == result) && (0U < touch_count) &&
        (TOUCH_2048_TOUCH_MAX_POINTS >= touch_count)) {
        const int32_t mapped_x =
            (int32_t)TOUCH_2048_LVGL_HOR_RES - 1 - (int32_t)point.y;
        const int32_t mapped_y = (int32_t)point.x;
        last_x = clamp_coordinate(mapped_x, TOUCH_2048_LVGL_HOR_RES - 1);
        last_y = clamp_coordinate(mapped_y, TOUCH_2048_LVGL_VER_RES - 1);
        data->state = LV_INDEV_STATE_PR;
    } else {
        if (ESP_OK != result) {
            increment_counter(&s_app.touch_read_errors);
        }
        data->state = LV_INDEV_STATE_REL;
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(TOUCH_2048_LVGL_TICK_MS);
}

static bool initialize_lvgl(bool touch_available)
{
    lv_init();
    const size_t buffer_pixels =
        (size_t)TOUCH_2048_LVGL_HOR_RES * TOUCH_2048_LCD_BUFFER_LINES;
    lv_color_t *buffer_1 = heap_caps_malloc(
        buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    lv_color_t *buffer_2 = heap_caps_malloc(
        buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if ((NULL == buffer_1) || (NULL == buffer_2)) {
        heap_caps_free(buffer_1);
        heap_caps_free(buffer_2);
        return false;
    }

    static lv_disp_draw_buf_t draw_buffer;
    lv_disp_draw_buf_init(&draw_buffer, buffer_1, buffer_2, buffer_pixels);

    static lv_disp_drv_t display_driver;
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = TOUCH_2048_LVGL_HOR_RES;
    display_driver.ver_res = TOUCH_2048_LVGL_VER_RES;
    display_driver.flush_cb = lvgl_flush_cb;
    display_driver.draw_buf = &draw_buffer;
    if (NULL == lv_disp_drv_register(&display_driver)) {
        return false;
    }

    if (touch_available) {
        static lv_indev_drv_t input_driver;
        lv_indev_drv_init(&input_driver);
        input_driver.type = LV_INDEV_TYPE_POINTER;
        input_driver.read_cb = lvgl_touch_read_cb;
        if (NULL == lv_indev_drv_register(&input_driver)) {
            return false;
        }
    }

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "touch_2048_tick",
        .skip_unhandled_events = true,
    };
    esp_err_t result = esp_timer_create(&tick_args, &s_lvgl_tick_timer);
    if (ESP_OK == result) {
        result = esp_timer_start_periodic(
            s_lvgl_tick_timer, TOUCH_2048_LVGL_TICK_MS * 1000U);
    }
    return ESP_OK == result;
}

static void log_health(const game_2048_app_t *app)
{
    if (NULL == app) {
        return;
    }
    ESP_LOGI(TAG,
             "[2048][health] valid=%" PRIu32 " invalid=%" PRIu32
             " touch_reject=%" PRIu32 " touch_err=%" PRIu32
             " score=%" PRIu32 " max=%" PRIu32 " empty=%u"
             " lcd_err=%" PRIu32 " nvs_err=%" PRIu32
             " game_err=%" PRIu32 " heap=%" PRIu32
             " min_heap=%" PRIu32,
             app->valid_moves, app->invalid_moves,
             app->touch_rejections, app->touch_read_errors,
             app->board.score, maximum_tile_value(&app->board),
             (unsigned int)game_2048_empty_count(&app->board),
             app->lcd_errors, app->nvs_errors, app->game_errors,
             esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
}

void app_main(void)
{
    memset(&s_app, 0, sizeof(s_app));
    ESP_LOGI(TAG,
             "[2048][startup] version=%s lcd_hz=%" PRIu32
             " buffers=2x%ux%u",
             TOUCH_2048_VERSION, (uint32_t)TOUCH_2048_LCD_PIXEL_CLOCK_HZ,
             (unsigned int)TOUCH_2048_LVGL_HOR_RES,
             (unsigned int)TOUCH_2048_LCD_BUFFER_LINES);

    esp_err_t result = board_laiwfs300_init();
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "[2048][error] op=board_init error=%s",
                 esp_err_to_name(result));
        return;
    }

    result = launcher_return_start_default();
    if (ESP_OK != result && ESP_ERR_NOT_SUPPORTED != result) {
        ESP_LOGW(TAG, "[2048][warn] launcher return unavailable: %s",
                 esp_err_to_name(result));
    }

    result = board_laiwfs300_display_init_with_config(
        TOUCH_2048_LCD_PIXEL_CLOCK_HZ, TOUCH_2048_LCD_BUFFER_LINES);
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "[2048][error] op=display_init error=%s",
                 esp_err_to_name(result));
        return;
    }
    result = display_hal_set_orientation(true, false, true);
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "[2048][error] op=display_orientation error=%s",
                 esp_err_to_name(result));
        return;
    }

    result = board_laiwfs300_touch_init();
    s_app.touch_available = (ESP_OK == result);
    if (!s_app.touch_available) {
        ESP_LOGW(TAG, "[2048][startup] touch=unavailable error=%s",
                 esp_err_to_name(result));
    }

    if (!initialize_lvgl(s_app.touch_available)) {
        ESP_LOGE(TAG, "[2048][error] op=lvgl_init");
        return;
    }

    initialize_storage(&s_app);
    if (!game_2048_new_game(&s_app.board, hardware_rng, NULL)) {
        ESP_LOGE(TAG, "[2048][error] op=new_game");
        return;
    }
    game_2048_snapshot_clear(&s_app.undo_snapshot);

    const game_2048_ui_callbacks_t callbacks = {
        .on_direction = direction_requested,
        .on_undo = undo_requested,
        .on_restart = restart_requested,
        .on_touch_rejected = touch_rejected,
        .ctx = &s_app,
    };
    if (!game_2048_ui_create(&callbacks)) {
        ESP_LOGE(TAG, "[2048][error] op=ui_create");
        return;
    }
    render_game(&s_app);

    s_app.next_health_log_us =
        esp_timer_get_time() + TOUCH_2048_HEALTH_INTERVAL_US;
    ESP_LOGI(TAG,
             "[2048][startup] ready touch=%s nvs=%s score=%" PRIu32
             " max=%" PRIu32 " empty=%u",
             s_app.touch_available ? "ready" : "disabled",
             s_app.nvs.ready ? "ready" : "disabled",
             s_app.board.score, maximum_tile_value(&s_app.board),
             (unsigned int)game_2048_empty_count(&s_app.board));

    while (true) {
        (void)lv_timer_handler();
        const int64_t now_us = esp_timer_get_time();
        if (now_us >= s_app.next_health_log_us) {
            log_health(&s_app);
            s_app.next_health_log_us =
                now_us + TOUCH_2048_HEALTH_INTERVAL_US;
        }
        vTaskDelay(pdMS_TO_TICKS(TOUCH_2048_LVGL_LOOP_DELAY_MS));
    }
}
