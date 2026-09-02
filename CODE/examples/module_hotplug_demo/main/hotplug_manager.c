#include "hotplug_manager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#define HOTPLUG_MAX_SLOTS 8
#define HOTPLUG_DEBOUNCE_COUNT 3

static const char *TAG = "hotplug_mgr";

typedef struct {
    hotplug_slot_t slot;
    hotplug_state_t last_state;
    uint8_t debounce_counter;
    hotplug_state_t pending_state;
} slot_entry_t;

static slot_entry_t s_slots[HOTPLUG_MAX_SLOTS];
static size_t s_slot_count;
static TaskHandle_t s_poll_task;
static uint32_t s_poll_interval_ms;
static volatile bool s_running;
static hotplug_event_cb_t s_event_cb;
static void *s_event_ctx;

static const char *state_name(hotplug_state_t state)
{
    switch (state) {
    case HOTPLUG_STATE_ABSENT:
        return "ABSENT";
    case HOTPLUG_STATE_PRESENT:
        return "PRESENT";
    case HOTPLUG_STATE_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

static void poll_task(void *arg)
{
    uint32_t tick_count = 0;
    while (s_running) {
        tick_count++;
        bool log_this_tick = (tick_count % 10 == 1);

        for (size_t i = 0; i < s_slot_count; i++) {
            slot_entry_t *entry = &s_slots[i];
            hotplug_state_t current = entry->slot.detect_fn();

            if (log_this_tick) {
                ESP_LOGI(TAG, "poll #%lu: [%s] detect=%s last=%s dbnc=%d",
                         (unsigned long)tick_count, entry->slot.name,
                         state_name(current), state_name(entry->last_state),
                         entry->debounce_counter);
            }

            if (HOTPLUG_STATE_UNKNOWN == current) {
                entry->debounce_counter = 0;
                continue;
            }

            if (current != entry->last_state) {
                if (current == entry->pending_state) {
                    entry->debounce_counter++;
                } else {
                    entry->pending_state = current;
                    entry->debounce_counter = 1;
                }

                if (entry->debounce_counter >= HOTPLUG_DEBOUNCE_COUNT) {
                    entry->last_state = current;
                    entry->debounce_counter = 0;
                    if (HOTPLUG_STATE_PRESENT == current) {
                        ESP_LOGI(TAG, "[HOTPLUG] %s INSERTED", entry->slot.name);
                    } else {
                        ESP_LOGI(TAG, "[HOTPLUG] %s REMOVED", entry->slot.name);
                    }
                    if (NULL != s_event_cb) {
                        s_event_cb(entry->slot.name, current, s_event_ctx);
                    }
                }
            } else {
                entry->debounce_counter = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(s_poll_interval_ms));
    }
    vTaskDelete(NULL);
}

esp_err_t hotplug_manager_register_slot(const hotplug_slot_t *slot)
{
    if (NULL == slot || NULL == slot->name || NULL == slot->detect_fn) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_slot_count >= HOTPLUG_MAX_SLOTS) {
        return ESP_ERR_NO_MEM;
    }
    s_slots[s_slot_count].slot = *slot;
    s_slots[s_slot_count].last_state = HOTPLUG_STATE_ABSENT;
    s_slots[s_slot_count].debounce_counter = 0;
    s_slots[s_slot_count].pending_state = HOTPLUG_STATE_ABSENT;
    s_slot_count++;
    return ESP_OK;
}

esp_err_t hotplug_manager_set_event_cb(hotplug_event_cb_t cb, void *user_ctx)
{
    s_event_cb = cb;
    s_event_ctx = user_ctx;
    return ESP_OK;
}

esp_err_t hotplug_manager_start(uint32_t poll_interval_ms)
{
    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    s_poll_interval_ms = poll_interval_ms;
    s_running = true;

    for (size_t i = 0; i < s_slot_count; i++) {
        hotplug_state_t initial_state = s_slots[i].slot.detect_fn();
        if (HOTPLUG_STATE_UNKNOWN == initial_state) {
            s_slots[i].last_state = HOTPLUG_STATE_ABSENT;
            ESP_LOGW(TAG, "[HOTPLUG] %s UNKNOWN at startup, keep ABSENT",
                     s_slots[i].slot.name);
            continue;
        }
        s_slots[i].last_state = initial_state;
        if (HOTPLUG_STATE_PRESENT == s_slots[i].last_state) {
            ESP_LOGI(TAG, "[HOTPLUG] %s already PRESENT at startup", s_slots[i].slot.name);
            if (NULL != s_event_cb) {
                s_event_cb(s_slots[i].slot.name, HOTPLUG_STATE_PRESENT, s_event_ctx);
            }
        } else {
            ESP_LOGI(TAG, "[HOTPLUG] %s ABSENT at startup", s_slots[i].slot.name);
        }
    }

    BaseType_t ret = xTaskCreate(poll_task, "hotplug_poll", 4096, NULL, 5, &s_poll_task);
    return (pdPASS == ret) ? ESP_OK : ESP_FAIL;
}

esp_err_t hotplug_manager_stop(void)
{
    s_running = false;
    return ESP_OK;
}

hotplug_state_t hotplug_manager_get_state(const char *name)
{
    for (size_t i = 0; i < s_slot_count; i++) {
        if (0 == strcmp(s_slots[i].slot.name, name)) {
            return s_slots[i].last_state;
        }
    }
    return HOTPLUG_STATE_ABSENT;
}
