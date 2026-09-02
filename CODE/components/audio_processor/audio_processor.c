#include "audio_processor.h"
#include "audio_processor_task_policy.h"

#include "esp_afe_sr_models.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_wn_iface.h"

#include <string.h>

#define WAKENET_THRESHOLD_DEFAULT 0.0f
#define WAKENET_THRESHOLD_MIN 0.4f
#define WAKENET_THRESHOLD_MAX 0.9999f
#define WAKENET_PRIMARY_INDEX 1
#define WAKENET_THRESHOLD_SET_SUCCESS 1

static const char *TAG = "audio_processor";

static const esp_afe_sr_iface_t *s_afe_iface;
static esp_afe_sr_data_t *s_afe_data;
static size_t s_feed_chunksize;
static size_t s_fetch_chunksize;
static uint8_t s_total_channels;
static float s_wakenet_threshold;

esp_err_t audio_processor_init(const audio_processor_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(NULL != cfg, ESP_ERR_INVALID_ARG, TAG, "cfg null");
    ESP_RETURN_ON_FALSE(NULL == s_afe_data, ESP_ERR_INVALID_STATE, TAG, "already init");
    ESP_RETURN_ON_FALSE(
        WAKENET_THRESHOLD_DEFAULT == cfg->wakenet_threshold ||
            (WAKENET_THRESHOLD_MIN <= cfg->wakenet_threshold &&
             WAKENET_THRESHOLD_MAX >= cfg->wakenet_threshold),
        ESP_ERR_INVALID_ARG, TAG, "invalid wakenet threshold");

    s_total_channels = cfg->mic_channels + cfg->ref_channels;

    char input_format[8] = {0};
    size_t idx = 0;
    for (uint8_t i = 0; i < cfg->mic_channels && idx < sizeof(input_format) - 1; i++) {
        input_format[idx++] = 'M';
    }
    for (uint8_t i = 0; i < cfg->ref_channels && idx < sizeof(input_format) - 1; i++) {
        input_format[idx++] = 'R';
    }

    srmodel_list_t *models = esp_srmodel_init("model");

    char *ns_model_name = NULL;
    if (cfg->enable_ns && NULL != models) {
        ns_model_name = esp_srmodel_filter(models, ESP_NSNET_PREFIX, NULL);
    }

    char *wn_model_name = NULL;
    if (cfg->enable_wakenet && NULL != models) {
        wn_model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
        ESP_LOGI(TAG, "wakenet model: %s", wn_model_name ? wn_model_name : "NOT FOUND");
    }

    afe_type_t afe_type = cfg->enable_wakenet ? AFE_TYPE_SR : AFE_TYPE_VC;
    const afe_mode_t afe_mode =
        AUDIO_PROCESSOR_AFE_MODE_LOW_COST ==
                audio_processor_resolve_afe_mode(cfg) ?
            AFE_MODE_LOW_COST : AFE_MODE_HIGH_PERF;
    afe_config_t *afe_config =
        afe_config_init(input_format, models, afe_type, afe_mode);
    ESP_RETURN_ON_FALSE(NULL != afe_config, ESP_ERR_NO_MEM, TAG, "afe_config_init");

    const audio_processor_task_policy_t task_policy = {
        .valid = cfg->afe_task_policy_valid,
        .core = cfg->afe_task_core,
        .priority = cfg->afe_task_priority,
    };
    ESP_RETURN_ON_FALSE(audio_processor_task_policy_is_valid(&task_policy),
                        ESP_ERR_INVALID_ARG, TAG, "invalid afe task policy");
    if (task_policy.valid) {
        afe_config->afe_perferred_core = (int)task_policy.core;
        afe_config->afe_perferred_priority = (int)task_policy.priority;
    }

    if (cfg->enable_aec) {
        afe_config->aec_init = true;
        afe_config->aec_mode = (0 != cfg->aec_mode) ? cfg->aec_mode : AEC_MODE_VOIP_HIGH_PERF;
    } else {
        afe_config->aec_init = false;
    }

    if (cfg->enable_ns && NULL != ns_model_name) {
        afe_config->ns_init = true;
        afe_config->ns_model_name = ns_model_name;
        afe_config->afe_ns_mode = AFE_NS_MODE_NET;
    } else {
        afe_config->ns_init = cfg->enable_ns;
    }

    if (cfg->enable_wakenet && NULL != wn_model_name) {
        afe_config->wakenet_init = true;
        afe_config->wakenet_model_name = wn_model_name;
    } else {
        afe_config->wakenet_init = false;
    }

    afe_config->vad_init = cfg->enable_vad;
    afe_config->vad_mode = VAD_MODE_0;
    afe_config->vad_min_noise_ms = 100;
    afe_config->agc_init = false;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

    s_afe_iface = esp_afe_handle_from_config(afe_config);
    ESP_RETURN_ON_FALSE(NULL != s_afe_iface, ESP_FAIL, TAG, "afe handle null");

    s_afe_data = s_afe_iface->create_from_config(afe_config);
    ESP_RETURN_ON_FALSE(NULL != s_afe_data, ESP_FAIL, TAG, "afe create failed");

    s_wakenet_threshold = cfg->wakenet_threshold;
    if (WAKENET_THRESHOLD_DEFAULT != s_wakenet_threshold) {
        const int threshold_result = s_afe_iface->set_wakenet_threshold(
            s_afe_data, WAKENET_PRIMARY_INDEX, s_wakenet_threshold);
        if (WAKENET_THRESHOLD_SET_SUCCESS != threshold_result) {
            s_afe_iface->destroy(s_afe_data);
            s_afe_data = NULL;
            s_afe_iface = NULL;
            s_wakenet_threshold = WAKENET_THRESHOLD_DEFAULT;
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "wakenet threshold index=%d value=%.3f result=%d",
                 WAKENET_PRIMARY_INDEX, (double)s_wakenet_threshold,
                 threshold_result);
    } else {
        ESP_LOGI(TAG, "wakenet threshold uses model default");
    }

    s_feed_chunksize = s_afe_iface->get_feed_chunksize(s_afe_data);
    s_fetch_chunksize = s_afe_iface->get_fetch_chunksize(s_afe_data);

    ESP_LOGI(TAG, "AFE initialized: format=%s ns=%d aec=%d vad=%d wn=%d feed=%u fetch=%u",
             input_format, cfg->enable_ns, cfg->enable_aec, cfg->enable_vad,
             cfg->enable_wakenet, (unsigned)s_feed_chunksize, (unsigned)s_fetch_chunksize);
    return ESP_OK;
}

size_t audio_processor_get_feed_chunksize(void)
{
    return s_feed_chunksize;
}

size_t audio_processor_get_fetch_chunksize(void)
{
    return s_fetch_chunksize;
}

esp_err_t audio_processor_feed(const int16_t *data, size_t samples_per_channel)
{
    ESP_RETURN_ON_FALSE(NULL != s_afe_data, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_FALSE(NULL != data, ESP_ERR_INVALID_ARG, TAG, "data null");

    s_afe_iface->feed(s_afe_data, (int16_t *)data);
    return ESP_OK;
}

esp_err_t audio_processor_fetch(int16_t *out, size_t *out_samples, bool *vad_active, bool *wakeup)
{
    ESP_RETURN_ON_FALSE(NULL != s_afe_data, ESP_ERR_INVALID_STATE, TAG, "not init");

    afe_fetch_result_t *res = s_afe_iface->fetch(s_afe_data);
    if (NULL == res || ESP_FAIL == res->ret_value) {
        return ESP_FAIL;
    }

    static uint32_t s_fetch_count = 0;
    s_fetch_count++;
    if (1 == s_fetch_count || 0 == (s_fetch_count % 200)) {
        ESP_LOGI(TAG, "fetch #%lu: vad=%d wakeup=%d data_size=%d",
                 (unsigned long)s_fetch_count, res->vad_state, res->wakeup_state, res->data_size);
    }
    if (WAKENET_DETECTED == res->wakeup_state) {
        ESP_LOGW(TAG,
                 ">>> WAKENET_DETECTED at fetch #%lu volume=%.1f word=%d model=%d length=%d configured_threshold=%.3f <<<",
                 (unsigned long)s_fetch_count, (double)res->data_volume,
                 res->wake_word_index, res->wakenet_model_index,
                 res->wake_word_length, (double)s_wakenet_threshold);
    }

    size_t samples = res->data_size / sizeof(int16_t);
    if (NULL != out) {
        memcpy(out, res->data, res->data_size);
    }
    if (NULL != out_samples) {
        *out_samples = samples;
    }
    if (NULL != vad_active) {
        *vad_active = (res->vad_state == VAD_SPEECH);
    }
    if (NULL != wakeup) {
        *wakeup = (res->wakeup_state == WAKENET_DETECTED);
    }
    return ESP_OK;
}

esp_err_t audio_processor_disable_aec(void)
{
    ESP_RETURN_ON_FALSE(NULL != s_afe_data && NULL != s_afe_iface, ESP_ERR_INVALID_STATE, TAG, "not init");
    int ret = s_afe_iface->disable_aec(s_afe_data);
    ESP_LOGI(TAG, "AEC disabled (ret=%d)", ret);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_processor_enable_aec(void)
{
    ESP_RETURN_ON_FALSE(NULL != s_afe_data && NULL != s_afe_iface, ESP_ERR_INVALID_STATE, TAG, "not init");
    int ret = s_afe_iface->enable_aec(s_afe_data);
    ESP_LOGI(TAG, "AEC enabled (ret=%d)", ret);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_processor_reset_buffer(void)
{
    ESP_RETURN_ON_FALSE(NULL != s_afe_data && NULL != s_afe_iface, ESP_ERR_INVALID_STATE, TAG, "not init");
    int ret = s_afe_iface->reset_buffer(s_afe_data);
    ESP_LOGI(TAG, "AFE buffer reset (ret=%d)", ret);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

void audio_processor_deinit(void)
{
    if (NULL != s_afe_data && NULL != s_afe_iface) {
        s_afe_iface->destroy(s_afe_data);
        s_afe_data = NULL;
        s_afe_iface = NULL;
    }
    s_feed_chunksize = 0;
    s_fetch_chunksize = 0;
    s_total_channels = 0;
    s_wakenet_threshold = WAKENET_THRESHOLD_DEFAULT;
    ESP_LOGI(TAG, "AFE deinitialized");
}
