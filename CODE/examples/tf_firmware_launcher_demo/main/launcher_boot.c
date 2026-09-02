#include "launcher_boot.h"

#include "board_laiwfs300.h"
#include "board_pins.h"
#include "io_expander.h"

#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "miniz.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "launcher_boot";

#define BOOT_ANIMATION_MAGIC "R5Z1"
#define BOOT_ANIMATION_VERSION 1U
#define BOOT_ANIMATION_HEADER_SIZE 24U
#define BOOT_ANIMATION_MAX_BYTES (4U * 1024U * 1024U)
#define BOOT_ANIMATION_MAX_FRAMES 200U
#define BOOT_ANIMATION_MIN_DELAY_MS 20U
#define BOOT_ANIMATION_MAX_DELAY_MS 1000U
#define BOOT_FRAME_HEADER_SIZE 8U
#define BOOT_ANIMATION_WIDTH BOARD_LAIWFS300_LCD_V_RES
#define BOOT_ANIMATION_HEIGHT BOARD_LAIWFS300_LCD_H_RES
#define BOOT_FRAME_BYTES (BOOT_ANIMATION_WIDTH * BOOT_ANIMATION_HEIGHT * sizeof(uint16_t))

#define BOOT_AUDIO_SAMPLE_RATE 16000U
#define BOOT_AUDIO_MAX_SECONDS 10U
#define BOOT_AUDIO_MAX_BYTES (BOOT_AUDIO_SAMPLE_RATE * BOOT_AUDIO_MAX_SECONDS * sizeof(int16_t))
#define BOOT_AUDIO_VOLUME 70
#define BOOT_AUDIO_TASK_STACK 4096
#define BOOT_AUDIO_TASK_PRIORITY 3
#define BOOT_AUDIO_CHUNK_BYTES 1024U
#define BOOT_AUDIO_SILENCE_SAMPLES 160U
#define BOOT_AMP_SETTLE_MS 20U

#define BOOT_PREPARE_TASK_STACK 6144
#define BOOT_PREPARE_TASK_PRIORITY 3
#define BOOT_LOADING_RENDER_MS 100U
#define BOOT_COMPLETION_POLL_MS 20U

#define BOOT_EVENT_ANIMATION_DONE BIT0
#define BOOT_EVENT_AUDIO_DONE BIT1
#define BOOT_EVENT_ALL_DONE (BOOT_EVENT_ANIMATION_DONE | BOOT_EVENT_AUDIO_DONE)

#if LV_COLOR_DEPTH != 16
#error "tf_firmware_launcher_demo boot animation requires LV_COLOR_DEPTH=16"
#endif

typedef struct __attribute__((packed)) {
    uint8_t magic[4];
    uint16_t version;
    uint16_t header_size;
    uint16_t width;
    uint16_t height;
    uint16_t frame_count;
    uint16_t frame_delay_ms;
    uint32_t payload_size;
    uint32_t payload_crc32;
} boot_animation_header_t;

typedef struct __attribute__((packed)) {
    uint32_t compressed_size;
    uint32_t raw_size;
} boot_frame_header_t;

typedef struct {
    const uint8_t *data;
    uint32_t compressed_size;
} boot_frame_t;

typedef struct {
    uint8_t *file_data;
    size_t file_size;
    uint8_t *canvas;
    tinfl_decompressor *decompressor;
    uint16_t frame_count;
    uint16_t frame_delay_ms;
    uint16_t next_frame;
    boot_frame_t frames[BOOT_ANIMATION_MAX_FRAMES];
    lv_img_dsc_t image_descriptor;
} boot_animation_t;

_Static_assert(BOOT_ANIMATION_HEADER_SIZE == sizeof(boot_animation_header_t),
               "boot animation header size mismatch");
_Static_assert(BOOT_FRAME_HEADER_SIZE == sizeof(boot_frame_header_t),
               "boot frame header size mismatch");

static launcher_boot_config_t s_config;
static EventGroupHandle_t s_boot_events;
static lv_obj_t *s_boot_container;
static lv_obj_t *s_boot_image;
static lv_obj_t *s_loading_label;
static lv_timer_t *s_frame_timer;
static lv_timer_t *s_completion_timer;
static launcher_boot_complete_cb_t s_complete_cb;
static void *s_complete_user_ctx;
static boot_animation_t s_animation;
static uint8_t *s_audio_data;
static size_t s_audio_size;
static int64_t s_prepare_started_at_us;
static int64_t s_play_started_at_us;
static bool s_started;
static bool s_finished;

static esp_err_t set_amplifier_enabled(bool enabled) {
    esp_err_t ret = io_expander_set_pin_direction(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
                                                  BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, true);
    if (ESP_OK != ret) {
        return ret;
    }
    return io_expander_write_pin(BOARD_LAIWFS300_IOEX_AMP_CTRL_PORT,
                                 BOARD_LAIWFS300_IOEX_AMP_CTRL_PIN, enabled);
}

static esp_err_t write_silence(esp_codec_dev_handle_t output_dev) {
    int16_t silence[BOOT_AUDIO_SILENCE_SAMPLES] = {0};
    return esp_codec_dev_write(output_dev, silence, sizeof(silence));
}

static esp_err_t play_audio(const uint8_t *pcm_data, size_t pcm_size) {
    if (NULL == pcm_data || 0U == pcm_size || 0U != (pcm_size % sizeof(int16_t))) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = board_laiwfs300_audio_init();
    if (ESP_OK != ret) {
        return ret;
    }

    esp_codec_dev_handle_t output_dev = board_laiwfs300_audio_get_output_dev();
    if (NULL == output_dev) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = esp_codec_dev_set_out_vol(output_dev, BOOT_AUDIO_VOLUME);
    if (ESP_OK != ret) {
        return ret;
    }
    ret = set_amplifier_enabled(true);
    if (ESP_OK != ret) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(BOOT_AMP_SETTLE_MS));
    ret = write_silence(output_dev);
    if (ESP_OK == ret) {
        const uint8_t *cursor = pcm_data;
        const uint8_t *end = pcm_data + pcm_size;
        while (cursor < end) {
            size_t remaining = (size_t)(end - cursor);
            size_t chunk_size =
                (BOOT_AUDIO_CHUNK_BYTES > remaining) ? remaining : BOOT_AUDIO_CHUNK_BYTES;
            ret = esp_codec_dev_write(output_dev, (void *)cursor, chunk_size);
            if (ESP_OK != ret) {
                break;
            }
            cursor += chunk_size;
        }
    }

    esp_err_t silence_ret = write_silence(output_dev);
    vTaskDelay(pdMS_TO_TICKS(BOOT_AMP_SETTLE_MS));
    esp_err_t amp_ret = set_amplifier_enabled(false);
    if (ESP_OK == ret && ESP_OK != silence_ret) {
        ret = silence_ret;
    }
    if (ESP_OK == ret && ESP_OK != amp_ret) {
        ret = amp_ret;
    }
    return ret;
}

static void release_boot_assets(void) {
    if (NULL != s_animation.canvas) {
        heap_caps_free(s_animation.canvas);
    }
    if (NULL != s_animation.decompressor) {
        heap_caps_free(s_animation.decompressor);
    }
    if (NULL != s_animation.file_data) {
        heap_caps_free(s_animation.file_data);
    }
    if (NULL != s_audio_data) {
        heap_caps_free(s_audio_data);
    }
    memset(&s_animation, 0, sizeof(s_animation));
    s_audio_data = NULL;
    s_audio_size = 0U;
}

static esp_err_t read_file_to_psram(const char *path, size_t max_size, uint8_t **data_out,
                                    size_t *size_out) {
    if (NULL == path || NULL == data_out || NULL == size_out) {
        return ESP_ERR_INVALID_ARG;
    }
    *data_out = NULL;
    *size_out = 0U;

    FILE *file = fopen(path, "rb");
    if (NULL == file) {
        return ESP_ERR_NOT_FOUND;
    }
    if (0 != fseek(file, 0L, SEEK_END)) {
        fclose(file);
        return ESP_FAIL;
    }
    long file_size = ftell(file);
    if (0L >= file_size || max_size < (size_t)file_size || 0 != fseek(file, 0L, SEEK_SET)) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *data = heap_caps_malloc((size_t)file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == data) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t bytes_read = fread(data, 1U, (size_t)file_size, file);
    int close_result = fclose(file);
    if ((size_t)file_size != bytes_read || 0 != close_result) {
        heap_caps_free(data);
        return ESP_FAIL;
    }

    *data_out = data;
    *size_out = bytes_read;
    return ESP_OK;
}

static esp_err_t validate_audio(void) {
    if (NULL == s_audio_data || 0U == s_audio_size || BOOT_AUDIO_MAX_BYTES < s_audio_size ||
        0U != (s_audio_size % sizeof(int16_t))) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t validate_animation(void) {
    if (NULL == s_animation.file_data || sizeof(boot_animation_header_t) > s_animation.file_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    boot_animation_header_t header = {0};
    memcpy(&header, s_animation.file_data, sizeof(header));
    if (0 != memcmp(header.magic, BOOT_ANIMATION_MAGIC, sizeof(header.magic)) ||
        BOOT_ANIMATION_VERSION != header.version ||
        BOOT_ANIMATION_HEADER_SIZE != header.header_size || BOOT_ANIMATION_WIDTH != header.width ||
        BOOT_ANIMATION_HEIGHT != header.height || 0U == header.frame_count ||
        BOOT_ANIMATION_MAX_FRAMES < header.frame_count ||
        BOOT_ANIMATION_MIN_DELAY_MS > header.frame_delay_ms ||
        BOOT_ANIMATION_MAX_DELAY_MS < header.frame_delay_ms ||
        header.payload_size != (s_animation.file_size - header.header_size)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const uint8_t *payload = s_animation.file_data + header.header_size;
    uint32_t actual_crc = esp_rom_crc32_le(0U, payload, header.payload_size);
    if (header.payload_crc32 != actual_crc) {
        return ESP_ERR_INVALID_CRC;
    }

    const uint8_t *cursor = payload;
    size_t remaining = header.payload_size;
    for (uint16_t index = 0U; index < header.frame_count; index++) {
        if (sizeof(boot_frame_header_t) > remaining) {
            return ESP_ERR_INVALID_SIZE;
        }

        boot_frame_header_t frame_header = {0};
        memcpy(&frame_header, cursor, sizeof(frame_header));
        cursor += sizeof(frame_header);
        remaining -= sizeof(frame_header);
        if (BOOT_FRAME_BYTES != frame_header.raw_size || 0U == frame_header.compressed_size ||
            remaining < frame_header.compressed_size) {
            return ESP_ERR_INVALID_SIZE;
        }

        s_animation.frames[index].data = cursor;
        s_animation.frames[index].compressed_size = frame_header.compressed_size;
        cursor += frame_header.compressed_size;
        remaining -= frame_header.compressed_size;
    }
    if (0U != remaining) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_animation.frame_count = header.frame_count;
    s_animation.frame_delay_ms = header.frame_delay_ms;
    return ESP_OK;
}

static esp_err_t decompress_frame(uint16_t frame_index) {
    if (NULL == s_animation.canvas || NULL == s_animation.decompressor ||
        s_animation.frame_count <= frame_index) {
        return ESP_ERR_INVALID_ARG;
    }

    const boot_frame_t *frame = &s_animation.frames[frame_index];
    size_t input_size = frame->compressed_size;
    size_t output_size = BOOT_FRAME_BYTES;
    tinfl_init(s_animation.decompressor);
    tinfl_status status = tinfl_decompress(
        s_animation.decompressor, frame->data, &input_size, s_animation.canvas, s_animation.canvas,
        &output_size, TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (TINFL_STATUS_DONE != status || BOOT_FRAME_BYTES != output_size) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static void finish_boot_sequence(void) {
    if (s_finished) {
        return;
    }
    s_finished = true;

    if (NULL != s_frame_timer) {
        lv_timer_del(s_frame_timer);
        s_frame_timer = NULL;
    }
    if (NULL != s_completion_timer) {
        lv_timer_del(s_completion_timer);
        s_completion_timer = NULL;
    }
    if (NULL != s_boot_container) {
        lv_obj_del(s_boot_container);
        s_boot_container = NULL;
        s_boot_image = NULL;
        s_loading_label = NULL;
    }

    release_boot_assets();
    if (NULL != s_boot_events) {
        vEventGroupDelete(s_boot_events);
        s_boot_events = NULL;
    }

    ESP_LOGI(TAG, "boot sequence complete");
    if (NULL != s_complete_cb) {
        s_complete_cb(s_complete_user_ctx);
    }
}

static void boot_completion_timer_cb(lv_timer_t *timer) {
    (void)timer;
    EventBits_t completed = xEventGroupGetBits(s_boot_events);
    if (BOOT_EVENT_ALL_DONE == (completed & BOOT_EVENT_ALL_DONE)) {
        finish_boot_sequence();
    }
}

static void finish_animation(void) {
    if (NULL != s_frame_timer) {
        lv_timer_del(s_frame_timer);
        s_frame_timer = NULL;
    }
    int64_t elapsed_ms = (esp_timer_get_time() - s_play_started_at_us) / 1000;
    ESP_LOGI(TAG, "boot animation complete in %lld ms", (long long)elapsed_ms);
    xEventGroupSetBits(s_boot_events, BOOT_EVENT_ANIMATION_DONE);
}

static void boot_frame_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_animation.frame_count <= s_animation.next_frame) {
        finish_animation();
        return;
    }

    esp_err_t ret = decompress_frame(s_animation.next_frame);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "boot animation frame %u failed: %s", (unsigned)s_animation.next_frame,
                 esp_err_to_name(ret));
        finish_animation();
        return;
    }

    s_animation.next_frame++;
    lv_obj_invalidate(s_boot_image);
}

static void boot_audio_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "boot sound playback started: %u bytes", (unsigned)s_audio_size);
    esp_err_t ret = play_audio(s_audio_data, s_audio_size);
    if (ESP_OK == ret) {
        ESP_LOGI(TAG, "boot sound playback complete");
    } else {
        ESP_LOGW(TAG, "boot sound skipped: %s", esp_err_to_name(ret));
        (void)set_amplifier_enabled(false);
    }

    xEventGroupSetBits(s_boot_events, BOOT_EVENT_AUDIO_DONE);
    vTaskDelete(NULL);
}

static esp_err_t start_animation(void) {
    s_animation.decompressor =
        heap_caps_calloc(1U, sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == s_animation.decompressor) {
        return ESP_ERR_NO_MEM;
    }

    s_animation.canvas = heap_caps_malloc(BOOT_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == s_animation.canvas) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = decompress_frame(0U);
    if (ESP_OK != ret) {
        return ret;
    }

    memset(&s_animation.image_descriptor, 0, sizeof(s_animation.image_descriptor));
    s_animation.image_descriptor.header.always_zero = 0;
    s_animation.image_descriptor.header.w = BOOT_ANIMATION_WIDTH;
    s_animation.image_descriptor.header.h = BOOT_ANIMATION_HEIGHT;
    s_animation.image_descriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_animation.image_descriptor.data_size = BOOT_FRAME_BYTES;
    s_animation.image_descriptor.data = s_animation.canvas;

    s_boot_image = lv_img_create(s_boot_container);
    if (NULL == s_boot_image) {
        return ESP_ERR_NO_MEM;
    }
    lv_img_set_src(s_boot_image, &s_animation.image_descriptor);
    lv_obj_center(s_boot_image);
    lv_obj_add_flag(s_loading_label, LV_OBJ_FLAG_HIDDEN);

    s_animation.next_frame = 1U;
    s_frame_timer = lv_timer_create(boot_frame_timer_cb, s_animation.frame_delay_ms, NULL);
    if (NULL == s_frame_timer) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "boot animation started: %u frames, %u ms/frame",
             (unsigned)s_animation.frame_count, (unsigned)s_animation.frame_delay_ms);
    return ESP_OK;
}

static void start_loaded_assets(void) {
    s_play_started_at_us = esp_timer_get_time();

    s_completion_timer = lv_timer_create(boot_completion_timer_cb, BOOT_COMPLETION_POLL_MS, NULL);
    if (NULL == s_completion_timer) {
        ESP_LOGW(TAG, "boot completion timer could not be created");
        finish_boot_sequence();
        return;
    }

    if (NULL != s_animation.file_data) {
        esp_err_t animation_ret = start_animation();
        if (ESP_OK != animation_ret) {
            ESP_LOGW(TAG, "boot animation skipped: %s", esp_err_to_name(animation_ret));
            xEventGroupSetBits(s_boot_events, BOOT_EVENT_ANIMATION_DONE);
        }
    } else {
        xEventGroupSetBits(s_boot_events, BOOT_EVENT_ANIMATION_DONE);
    }

    if (NULL != s_audio_data) {
        BaseType_t task_created =
            xTaskCreate(boot_audio_task, "launcher_audio", BOOT_AUDIO_TASK_STACK, NULL,
                        BOOT_AUDIO_TASK_PRIORITY, NULL);
        if (pdPASS != task_created) {
            ESP_LOGW(TAG, "boot audio task could not be created");
            xEventGroupSetBits(s_boot_events, BOOT_EVENT_AUDIO_DONE);
        }
    } else {
        xEventGroupSetBits(s_boot_events, BOOT_EVENT_AUDIO_DONE);
    }

    EventBits_t completed = xEventGroupGetBits(s_boot_events);
    if (BOOT_EVENT_ALL_DONE == (completed & BOOT_EVENT_ALL_DONE)) {
        finish_boot_sequence();
    }
}

static void discard_invalid_animation(esp_err_t validation_ret) {
    if (ESP_OK == validation_ret) {
        return;
    }
    ESP_LOGW(TAG, "boot animation asset invalid: %s", esp_err_to_name(validation_ret));
    heap_caps_free(s_animation.file_data);
    memset(&s_animation, 0, sizeof(s_animation));
}

static void discard_invalid_audio(esp_err_t validation_ret) {
    if (ESP_OK == validation_ret) {
        return;
    }
    ESP_LOGW(TAG, "boot audio asset invalid: %s", esp_err_to_name(validation_ret));
    heap_caps_free(s_audio_data);
    s_audio_data = NULL;
    s_audio_size = 0U;
}

static void boot_prepare_task(void *arg) {
    (void)arg;
    esp_err_t mount_ret = ESP_OK;
    esp_err_t animation_ret = ESP_FAIL;
    esp_err_t audio_ret = ESP_FAIL;

    vTaskDelay(pdMS_TO_TICKS(BOOT_LOADING_RENDER_MS));
    xSemaphoreTake(s_config.spi_mutex, portMAX_DELAY);
    if (!storage_hal_is_mounted()) {
        mount_ret = storage_hal_init(&s_config.storage_config);
    }
    if (ESP_OK == mount_ret) {
        animation_ret = read_file_to_psram(s_config.animation_path, BOOT_ANIMATION_MAX_BYTES,
                                           &s_animation.file_data, &s_animation.file_size);
        audio_ret = read_file_to_psram(s_config.audio_path, BOOT_AUDIO_MAX_BYTES, &s_audio_data,
                                       &s_audio_size);
    }
    xSemaphoreGive(s_config.spi_mutex);

    if (ESP_OK != mount_ret) {
        ESP_LOGW(TAG, "TF mount for boot assets failed: %s", esp_err_to_name(mount_ret));
    } else {
        if (ESP_OK != animation_ret) {
            ESP_LOGW(TAG, "boot animation asset unavailable: %s", esp_err_to_name(animation_ret));
        } else {
            discard_invalid_animation(validate_animation());
        }
        if (ESP_OK != audio_ret) {
            ESP_LOGW(TAG, "boot audio asset unavailable: %s", esp_err_to_name(audio_ret));
        } else {
            discard_invalid_audio(validate_audio());
        }
    }

    int64_t elapsed_ms = (esp_timer_get_time() - s_prepare_started_at_us) / 1000;
    ESP_LOGI(TAG, "boot assets prepared in %lld ms: animation=%u audio=%u", (long long)elapsed_ms,
             (unsigned)s_animation.file_size, (unsigned)s_audio_size);

    xSemaphoreTakeRecursive(s_config.lvgl_mutex, portMAX_DELAY);
    start_loaded_assets();
    xSemaphoreGiveRecursive(s_config.lvgl_mutex);
    vTaskDelete(NULL);
}

static void create_loading_screen(void) {
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xf7f8fa), 0);

    s_boot_container = lv_obj_create(screen);
    lv_obj_set_size(s_boot_container, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_boot_container);
    lv_obj_clear_flag(s_boot_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_boot_container, 0, 0);
    lv_obj_set_style_border_width(s_boot_container, 0, 0);
    lv_obj_set_style_pad_all(s_boot_container, 0, 0);
    lv_obj_set_style_bg_color(s_boot_container, lv_color_hex(0xf7f8fa), 0);

    lv_obj_t *title = lv_label_create(s_boot_container);
    lv_label_set_text(title, "Demo Hub");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x2563eb), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -12);

    s_loading_label = lv_label_create(s_boot_container);
    lv_label_set_text(s_loading_label, "Loading...");
    lv_obj_set_style_text_color(s_loading_label, lv_color_hex(0x64748b), 0);
    lv_obj_align(s_loading_label, LV_ALIGN_CENTER, 0, 18);
}

esp_err_t launcher_boot_start(const launcher_boot_config_t *config,
                              launcher_boot_complete_cb_t complete_cb, void *user_ctx) {
    if (NULL == config || NULL == config->storage_config.mount_point ||
        NULL == config->animation_path || NULL == config->audio_path ||
        NULL == config->lvgl_mutex || NULL == config->spi_mutex) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    s_started = true;
    s_finished = false;
    s_config = *config;
    s_complete_cb = complete_cb;
    s_complete_user_ctx = user_ctx;
    s_prepare_started_at_us = esp_timer_get_time();
    s_boot_events = xEventGroupCreate();
    if (NULL == s_boot_events) {
        s_started = false;
        return ESP_ERR_NO_MEM;
    }

    create_loading_screen();
    BaseType_t task_created =
        xTaskCreate(boot_prepare_task, "launcher_prepare", BOOT_PREPARE_TASK_STACK, NULL,
                    BOOT_PREPARE_TASK_PRIORITY, NULL);
    if (pdPASS != task_created) {
        lv_obj_del(s_boot_container);
        s_boot_container = NULL;
        s_loading_label = NULL;
        vEventGroupDelete(s_boot_events);
        s_boot_events = NULL;
        s_started = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "boot asset preload started from TF");
    return ESP_OK;
}
