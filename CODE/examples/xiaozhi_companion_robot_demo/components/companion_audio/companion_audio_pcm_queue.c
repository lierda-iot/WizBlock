#include "companion_audio_pcm_queue.h"

#include <string.h>

static bool token_is_valid(const companion_audio_token_t *token)
{
    return NULL != token && 0U != token->generation &&
           0U != token->wake_seq && 0U != token->request_id;
}

esp_err_t companion_audio_pcm_queue_init(
    companion_audio_pcm_queue_t *queue,
    companion_audio_pcm_frame_t *storage, size_t capacity)
{
    if (NULL == queue || NULL == storage || 0U == capacity) {
        return ESP_ERR_INVALID_ARG;
    }
    *queue = (companion_audio_pcm_queue_t){
        .storage = storage,
        .capacity = capacity,
    };
    return ESP_OK;
}

esp_err_t companion_audio_pcm_queue_push(
    companion_audio_pcm_queue_t *queue, const int16_t *samples,
    const companion_audio_token_t *token)
{
    if (NULL == queue || NULL == queue->storage || 0U == queue->capacity ||
        NULL == samples || !token_is_valid(token)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (queue->count >= queue->capacity) {
        queue->drops++;
        return ESP_ERR_TIMEOUT;
    }
    companion_audio_pcm_frame_t *frame =
        &queue->storage[queue->write_index];
    memcpy(frame->samples, samples, sizeof(frame->samples));
    frame->token = *token;
    queue->write_index = (queue->write_index + 1U) % queue->capacity;
    queue->count++;
    if (queue->count > queue->high_water) {
        queue->high_water = queue->count;
    }
    return ESP_OK;
}

esp_err_t companion_audio_pcm_queue_pop(
    companion_audio_pcm_queue_t *queue,
    companion_audio_pcm_frame_t *frame)
{
    if (NULL == queue || NULL == queue->storage || 0U == queue->capacity ||
        NULL == frame) {
        return ESP_ERR_INVALID_ARG;
    }
    if (0U == queue->count) {
        return ESP_ERR_INVALID_STATE;
    }
    *frame = queue->storage[queue->read_index];
    queue->read_index = (queue->read_index + 1U) % queue->capacity;
    queue->count--;
    return ESP_OK;
}

void companion_audio_pcm_queue_clear(companion_audio_pcm_queue_t *queue)
{
    if (NULL == queue || NULL == queue->storage || 0U == queue->capacity) {
        return;
    }
    queue->read_index = 0U;
    queue->write_index = 0U;
    queue->count = 0U;
}

size_t companion_audio_pcm_queue_count(
    const companion_audio_pcm_queue_t *queue)
{
    return (NULL != queue) ? queue->count : 0U;
}
