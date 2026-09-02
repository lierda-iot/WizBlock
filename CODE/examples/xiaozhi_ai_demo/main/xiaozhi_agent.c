#include "xiaozhi_agent.h"
#include "xiaozhi_audio.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

static const char *TAG = "xiaozhi_agent";

#define WS_BUFFER_SIZE          15032
#define JSON_BUF_SIZE           512
#define AGENT_TASK_STACK        (8 * 1024)
#define AGENT_TASK_PRIO         4
#define WS_CONNECT_TIMEOUT_MS   15000
#define HELLO_TIMEOUT_MS        10000
#define RETRY_BASE_MS           1000
#define RETRY_MAX_MS            30000
#define PROTO_VERSION           3
#define PROTO_HDR_SIZE          4

typedef enum {
    EVT_WAKE_WORD = 0,
    EVT_VAD_END,
    EVT_WS_CONNECTED,
    EVT_WS_DISCONNECTED,
    EVT_WS_TEXT,
    EVT_WS_BINARY,
} agent_evt_type_t;

typedef struct {
    agent_evt_type_t type;
    uint8_t *data;
    int len;
} agent_evt_t;

static xiaozhi_agent_config_t s_config;
static esp_websocket_client_handle_t s_ws_client;
static volatile xiaozhi_agent_state_t s_state = XIAOZHI_STATE_IDLE;
static char s_ws_url[256];
static char s_ws_token[256];
static char s_session_id[64];
static QueueHandle_t s_evt_queue;
static TaskHandle_t s_agent_task;
static volatile bool s_running;
static uint32_t s_tx_frame_count;

static void set_state(xiaozhi_agent_state_t new_state)
{
    if (s_state == new_state) {
        return;
    }
    ESP_LOGI(TAG, "state: %d -> %d", (int)s_state, (int)new_state);
    s_state = new_state;
    if (NULL != s_config.on_state_change) {
        s_config.on_state_change(new_state, s_config.user_ctx);
    }
}

static void post_evt(agent_evt_type_t type, const uint8_t *data, int len)
{
    agent_evt_t evt = {.type = type, .data = NULL, .len = 0};
    if (NULL != data && len > 0) {
        evt.data = heap_caps_malloc((size_t)len, MALLOC_CAP_SPIRAM);
        if (NULL != evt.data) {
            memcpy(evt.data, data, (size_t)len);
            evt.len = len;
        }
    }
    if (pdTRUE != xQueueSend(s_evt_queue, &evt, 0)) {
        if (NULL != evt.data) {
            heap_caps_free(evt.data);
        }
    }
}

/* ──── Version Check ──── */

static esp_err_t version_check(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddStringToObject(root, "language", s_config.lang);
    cJSON_AddNumberToObject(root, "flash_size", 8388608);
    cJSON_AddStringToObject(root, "mac_address", s_config.device_mac);
    cJSON_AddStringToObject(root, "chip_model_name", "esp32s3");

    cJSON *app = cJSON_AddObjectToObject(root, "application");
    cJSON_AddStringToObject(app, "name", "xiaozhi_ai_demo");
    cJSON_AddStringToObject(app, "version", "0.1.0");
    cJSON_AddStringToObject(app, "idf_version", "v5.5.4");

    cJSON *board = cJSON_AddObjectToObject(root, "board");
    cJSON_AddStringToObject(board, "type", "laiwfs300");
    cJSON_AddStringToObject(board, "name", "L-AIWFS300");

    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (NULL == post_data) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t http_cfg = {
        .url = s_config.ota_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (NULL == client) {
        cJSON_free(post_data);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Device-Id", s_config.device_mac);
    esp_http_client_set_post_field(client, post_data, (int)strlen(post_data));

    ESP_LOGI(TAG, "version_check: POST %s", s_config.ota_url);
    esp_err_t err = esp_http_client_perform(client);
    cJSON_free(post_data);

    if (ESP_OK != err) {
        ESP_LOGE(TAG, "version_check HTTP failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    int content_len = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "version_check: status=%d, len=%d", status, content_len);

    if (200 != status) {
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    char *resp_buf = heap_caps_malloc((size_t)(content_len + 1), MALLOC_CAP_SPIRAM);
    if (NULL == resp_buf) {
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    int read_len = esp_http_client_read_response(client, resp_buf, content_len);
    resp_buf[read_len > 0 ? read_len : 0] = '\0';
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "version_check resp: %s", resp_buf);

    cJSON *resp = cJSON_Parse(resp_buf);
    heap_caps_free(resp_buf);
    if (NULL == resp) {
        ESP_LOGE(TAG, "version_check: JSON parse failed");
        return ESP_FAIL;
    }

    s_ws_url[0] = '\0';
    s_ws_token[0] = '\0';

    cJSON *ws_obj = cJSON_GetObjectItem(resp, "websocket");
    if (NULL != ws_obj && cJSON_IsObject(ws_obj)) {
        cJSON *url_item = cJSON_GetObjectItem(ws_obj, "url");
        if (NULL != url_item && cJSON_IsString(url_item)) {
            strncpy(s_ws_url, url_item->valuestring, sizeof(s_ws_url) - 1);
        }
        cJSON *token_item = cJSON_GetObjectItem(ws_obj, "token");
        if (NULL != token_item && cJSON_IsString(token_item)) {
            strncpy(s_ws_token, token_item->valuestring, sizeof(s_ws_token) - 1);
        }
    }

    cJSON *activation = cJSON_GetObjectItem(resp, "activation");
    if (NULL != activation && cJSON_IsObject(activation)) {
        cJSON *code = cJSON_GetObjectItem(activation, "code");
        if (NULL != code && cJSON_IsString(code)) {
            ESP_LOGW(TAG, "=== ACTIVATION REQUIRED ===");
            ESP_LOGW(TAG, "Activation code: %s", code->valuestring);
            ESP_LOGW(TAG, "Please activate at the XiaoZhi platform.");
        }
    }

    cJSON_Delete(resp);

    if (s_ws_url[0] == '\0') {
        ESP_LOGE(TAG, "version_check: no websocket URL in response");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "WS URL: %s", s_ws_url);
    return ESP_OK;
}

/* ──── WebSocket ──── */

static void ws_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *ws_data = (esp_websocket_event_data_t *)event_data;
    (void)arg;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        post_evt(EVT_WS_CONNECTED, NULL, 0);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        post_evt(EVT_WS_DISCONNECTED, NULL, 0);
        break;
    case WEBSOCKET_EVENT_DATA:
        if (0x01 == ws_data->op_code) {
            post_evt(EVT_WS_TEXT, (const uint8_t *)ws_data->data_ptr, ws_data->data_len);
        } else if (0x02 == ws_data->op_code) {
            post_evt(EVT_WS_BINARY, (const uint8_t *)ws_data->data_ptr, ws_data->data_len);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WS error");
        post_evt(EVT_WS_DISCONNECTED, NULL, 0);
        break;
    default:
        break;
    }
}

static esp_err_t ws_connect(void)
{
    if (s_ws_url[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    char auth_header[300];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_ws_token);

    char proto_ver[4];
    snprintf(proto_ver, sizeof(proto_ver), "%d", PROTO_VERSION);

    esp_websocket_client_config_t ws_cfg = {
        .uri = s_ws_url,
        .buffer_size = WS_BUFFER_SIZE,
        .network_timeout_ms = WS_CONNECT_TIMEOUT_MS,
    };
    s_ws_client = esp_websocket_client_init(&ws_cfg);
    if (NULL == s_ws_client) {
        return ESP_FAIL;
    }

    esp_websocket_client_append_header(s_ws_client, "Authorization", auth_header);
    esp_websocket_client_append_header(s_ws_client, "Protocol-Version", proto_ver);
    esp_websocket_client_append_header(s_ws_client, "Device-Id", s_config.device_mac);

    esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY,
                                  ws_event_handler, NULL);

    ESP_LOGI(TAG, "WS connecting to: %s", s_ws_url);
    esp_err_t ret = esp_websocket_client_start(s_ws_client);
    if (ESP_OK != ret) {
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
        return ret;
    }
    return ESP_OK;
}

static void ws_disconnect(void)
{
    if (NULL != s_ws_client) {
        esp_websocket_client_close(s_ws_client, pdMS_TO_TICKS(2000));
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
    }
}

static esp_err_t ws_send_hello(void)
{
    cJSON *hello = cJSON_CreateObject();
    cJSON_AddStringToObject(hello, "type", "hello");
    cJSON_AddNumberToObject(hello, "version", PROTO_VERSION);
    cJSON_AddStringToObject(hello, "transport", "websocket");

    cJSON *audio = cJSON_AddObjectToObject(hello, "audio_params");
    cJSON_AddStringToObject(audio, "format", "opus");
    cJSON_AddNumberToObject(audio, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio, "channels", 1);
    cJSON_AddNumberToObject(audio, "frame_duration", 60);

    char *json_str = cJSON_PrintUnformatted(hello);
    cJSON_Delete(hello);
    if (NULL == json_str) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Sending hello: %s", json_str);
    int sent = esp_websocket_client_send_text(s_ws_client, json_str, (int)strlen(json_str),
                                              pdMS_TO_TICKS(5000));
    cJSON_free(json_str);
    return (sent > 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t ws_send_json_msg(const char *type, const char *extra_fields)
{
    if (NULL == s_ws_client || !esp_websocket_client_is_connected(s_ws_client)) {
        return ESP_ERR_INVALID_STATE;
    }

    char buf[JSON_BUF_SIZE];
    int len;
    if (s_session_id[0] != '\0' && NULL != extra_fields) {
        len = snprintf(buf, sizeof(buf), "{\"session_id\":\"%s\",\"type\":\"%s\",%s}",
                       s_session_id, type, extra_fields);
    } else if (s_session_id[0] != '\0') {
        len = snprintf(buf, sizeof(buf), "{\"session_id\":\"%s\",\"type\":\"%s\"}",
                       s_session_id, type);
    } else if (NULL != extra_fields) {
        len = snprintf(buf, sizeof(buf), "{\"type\":\"%s\",%s}", type, extra_fields);
    } else {
        len = snprintf(buf, sizeof(buf), "{\"type\":\"%s\"}", type);
    }

    ESP_LOGD(TAG, "TX JSON: %s", buf);
    int sent = esp_websocket_client_send_text(s_ws_client, buf, len, pdMS_TO_TICKS(1000));
    return (sent > 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t ws_send_audio_frame(const uint8_t *opus_data, int opus_len)
{
    if (NULL == s_ws_client || !esp_websocket_client_is_connected(s_ws_client)) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t hdr[PROTO_HDR_SIZE];
    hdr[0] = 0;
    hdr[1] = 0;
    uint16_t plen = htons((uint16_t)opus_len);
    memcpy(&hdr[2], &plen, 2);

    int total = PROTO_HDR_SIZE + opus_len;
    uint8_t *frame = heap_caps_malloc((size_t)total, MALLOC_CAP_SPIRAM);
    if (NULL == frame) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(frame, hdr, PROTO_HDR_SIZE);
    memcpy(frame + PROTO_HDR_SIZE, opus_data, (size_t)opus_len);

    int sent = esp_websocket_client_send_bin(s_ws_client, (const char *)frame, total,
                                             pdMS_TO_TICKS(1000));
    heap_caps_free(frame);
    return (sent > 0) ? ESP_OK : ESP_FAIL;
}

/* ──── Message handling ──── */

static void handle_server_hello(cJSON *root)
{
    cJSON *sid = cJSON_GetObjectItem(root, "session_id");
    if (NULL != sid && cJSON_IsString(sid)) {
        strncpy(s_session_id, sid->valuestring, sizeof(s_session_id) - 1);
        ESP_LOGI(TAG, "Server hello, session_id=%s", s_session_id);
    }
}

static void handle_text_message(const uint8_t *data, int len)
{
    char *json_str = heap_caps_malloc((size_t)(len + 1), MALLOC_CAP_SPIRAM);
    if (NULL == json_str) {
        return;
    }
    memcpy(json_str, data, (size_t)len);
    json_str[len] = '\0';

    ESP_LOGD(TAG, "RX JSON: %s", json_str);

    cJSON *root = cJSON_Parse(json_str);
    heap_caps_free(json_str);
    if (NULL == root) {
        return;
    }

    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    if (NULL == type_item || !cJSON_IsString(type_item)) {
        cJSON_Delete(root);
        return;
    }
    const char *type = type_item->valuestring;

    if (0 == strcmp(type, "hello")) {
        handle_server_hello(root);
    } else if (0 == strcmp(type, "tts")) {
        cJSON *state = cJSON_GetObjectItem(root, "state");
        if (NULL != state && cJSON_IsString(state)) {
            if (0 == strcmp(state->valuestring, "start")) {
                ESP_LOGI(TAG, "TTS start");
                set_state(XIAOZHI_STATE_SPEAKING);
            } else if (0 == strcmp(state->valuestring, "stop")) {
                ESP_LOGI(TAG, "TTS stop");
                xiaozhi_audio_play_stop();
                set_state(XIAOZHI_STATE_IDLE);
            }
        }
    } else if (0 == strcmp(type, "stt")) {
        cJSON *text = cJSON_GetObjectItem(root, "text");
        if (NULL != text && cJSON_IsString(text)) {
            ESP_LOGI(TAG, "STT: %s", text->valuestring);
        }
    } else if (0 == strcmp(type, "goodbye")) {
        ESP_LOGI(TAG, "Server goodbye");
        xiaozhi_audio_play_stop();
        s_session_id[0] = '\0';
        set_state(XIAOZHI_STATE_IDLE);
    }

    cJSON_Delete(root);
}

static void handle_binary_message(const uint8_t *data, int len)
{
    if (len < PROTO_HDR_SIZE) {
        return;
    }

    uint8_t frame_type = data[0];
    uint16_t payload_size;
    memcpy(&payload_size, &data[2], 2);
    payload_size = ntohs(payload_size);

    if (PROTO_HDR_SIZE + payload_size > (uint16_t)len) {
        ESP_LOGW(TAG, "RX binary: truncated frame");
        return;
    }

    const uint8_t *payload = data + PROTO_HDR_SIZE;

    if (0 == frame_type) {
        if (s_state == XIAOZHI_STATE_SPEAKING) {
            xiaozhi_audio_play_opus(payload, (int)payload_size);
        }
    } else if (1 == frame_type) {
        handle_text_message(payload, (int)payload_size);
    }
}

/* ──── Agent task ──── */

static void print_heap_info(void)
{
    ESP_LOGI(TAG, "Heap free: internal=%lu, PSRAM=%lu",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void agent_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "agent_task started");
    print_heap_info();

    set_state(XIAOZHI_STATE_CONNECTING);

    uint32_t retry_delay_ms = RETRY_BASE_MS;
    while (s_running) {
        esp_err_t ret = version_check();
        if (ESP_OK == ret) {
            break;
        }
        ESP_LOGW(TAG, "version_check failed, retry in %lu ms", (unsigned long)retry_delay_ms);
        vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
        retry_delay_ms = retry_delay_ms * 2;
        if (retry_delay_ms > RETRY_MAX_MS) {
            retry_delay_ms = RETRY_MAX_MS;
        }
    }

    if (!s_running) {
        vTaskDelete(NULL);
        return;
    }

    esp_err_t ret = ws_connect();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "WS connect failed");
        set_state(XIAOZHI_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }

    bool hello_received = false;
    TickType_t last_heap_print = xTaskGetTickCount();

    while (s_running) {
        agent_evt_t evt;
        if (pdTRUE != xQueueReceive(s_evt_queue, &evt, pdMS_TO_TICKS(1000))) {
            if ((xTaskGetTickCount() - last_heap_print) > pdMS_TO_TICKS(30000)) {
                print_heap_info();
                last_heap_print = xTaskGetTickCount();
            }
            continue;
        }

        switch (evt.type) {
        case EVT_WS_CONNECTED:
            ESP_LOGI(TAG, "WS connected, sending hello");
            ws_send_hello();
            break;

        case EVT_WS_DISCONNECTED:
            ESP_LOGW(TAG, "WS disconnected, will reconnect");
            ws_disconnect();
            xiaozhi_audio_play_stop();
            s_session_id[0] = '\0';
            set_state(XIAOZHI_STATE_CONNECTING);
            hello_received = false;
            vTaskDelay(pdMS_TO_TICKS(2000));
            if (s_running) {
                retry_delay_ms = RETRY_BASE_MS;
                while (s_running) {
                    ret = version_check();
                    if (ESP_OK == ret) {
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
                    retry_delay_ms = retry_delay_ms * 2;
                    if (retry_delay_ms > RETRY_MAX_MS) {
                        retry_delay_ms = RETRY_MAX_MS;
                    }
                }
                if (s_running) {
                    ws_connect();
                }
            }
            break;

        case EVT_WS_TEXT:
            if (NULL != evt.data) {
                handle_text_message(evt.data, evt.len);
                if (!hello_received && s_session_id[0] != '\0') {
                    hello_received = true;
                    ESP_LOGI(TAG, "Hello exchange complete, ready for wake word");
                    set_state(XIAOZHI_STATE_IDLE);
                }
            }
            break;

        case EVT_WS_BINARY:
            if (NULL != evt.data) {
                handle_binary_message(evt.data, evt.len);
            }
            break;

        case EVT_WAKE_WORD:
            ESP_LOGI(TAG, "Wake word received by agent");
            if (s_state == XIAOZHI_STATE_SPEAKING) {
                ESP_LOGI(TAG, "Interrupting TTS playback");
                ws_send_json_msg("abort", "\"reason\":\"wake_word_detected\"");
                xiaozhi_audio_play_stop();
            }
            set_state(XIAOZHI_STATE_LISTENING);
            ws_send_json_msg("listen", "\"state\":\"start\",\"mode\":\"auto\"");
            s_tx_frame_count = 0;
            break;

        case EVT_VAD_END:
            ESP_LOGI(TAG, "VAD end, stop listening (sent %lu frames)",
                     (unsigned long)s_tx_frame_count);
            set_state(XIAOZHI_STATE_PROCESSING);
            ws_send_json_msg("listen", "\"state\":\"stop\"");
            break;
        }

        if (NULL != evt.data) {
            heap_caps_free(evt.data);
        }
    }

    ws_disconnect();
    ESP_LOGI(TAG, "agent_task exiting");
    vTaskDelete(NULL);
}

/* ──── Public API ──── */

esp_err_t xiaozhi_agent_init(const xiaozhi_agent_config_t *config)
{
    if (NULL == config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    s_state = XIAOZHI_STATE_IDLE;
    s_ws_url[0] = '\0';
    s_ws_token[0] = '\0';
    s_session_id[0] = '\0';
    s_running = false;
    s_tx_frame_count = 0;

    s_evt_queue = xQueueCreate(16, sizeof(agent_evt_t));
    if (NULL == s_evt_queue) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t xiaozhi_agent_start(void)
{
    s_running = true;
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        agent_task, "agent_main", AGENT_TASK_STACK, NULL,
        AGENT_TASK_PRIO, &s_agent_task, tskNO_AFFINITY, MALLOC_CAP_SPIRAM);
    return (pdPASS == ret) ? ESP_OK : ESP_FAIL;
}

esp_err_t xiaozhi_agent_stop(void)
{
    s_running = false;
    vTaskDelay(pdMS_TO_TICKS(500));
    ws_disconnect();
    set_state(XIAOZHI_STATE_IDLE);
    return ESP_OK;
}

xiaozhi_agent_state_t xiaozhi_agent_get_state(void)
{
    return s_state;
}

esp_err_t xiaozhi_agent_send_audio(const uint8_t *data, int len)
{
    if (s_state != XIAOZHI_STATE_LISTENING) {
        return ESP_OK;
    }
    s_tx_frame_count++;
    if (0 == (s_tx_frame_count % 100)) {
        ESP_LOGI(TAG, "Audio TX: %lu frames sent", (unsigned long)s_tx_frame_count);
    }
    return ws_send_audio_frame(data, len);
}

esp_err_t xiaozhi_agent_notify_vad_end(void)
{
    post_evt(EVT_VAD_END, NULL, 0);
    return ESP_OK;
}

esp_err_t xiaozhi_agent_notify_wake_word(void)
{
    post_evt(EVT_WAKE_WORD, NULL, 0);
    return ESP_OK;
}
