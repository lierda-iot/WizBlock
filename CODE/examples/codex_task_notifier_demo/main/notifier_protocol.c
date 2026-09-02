#include "notifier_protocol.h"

#include "jsmn.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NOTIFIER_JSON_MAX_TOKENS 768U
#define FIELD_TEXT_MAX_BYTES     16U

typedef struct {
    const char *json;
    size_t length;
    const jsmntok_t *tokens;
    int token_count;
    notifier_protocol_error_t *error;
} parse_context_t;

static bool fail(parse_context_t *context, notifier_protocol_error_t error)
{
    if (NULL != context && NULL != context->error) {
        *context->error = error;
    }
    return false;
}

static bool is_whitespace(char value)
{
    return ' ' == value || '\t' == value || '\r' == value || '\n' == value;
}

static bool validate_utf8(const char *value, size_t length)
{
    size_t index = 0U;

    while (index < length) {
        const uint8_t first = (uint8_t)value[index];
        uint32_t codepoint = 0U;
        size_t continuation_count = 0U;
        uint32_t minimum = 0U;

        if (first < 0x80U) {
            if (0U == first) {
                return false;
            }
            index++;
            continue;
        }
        if ((first & 0xE0U) == 0xC0U) {
            codepoint = first & 0x1FU;
            continuation_count = 1U;
            minimum = 0x80U;
        } else if ((first & 0xF0U) == 0xE0U) {
            codepoint = first & 0x0FU;
            continuation_count = 2U;
            minimum = 0x800U;
        } else if ((first & 0xF8U) == 0xF0U) {
            codepoint = first & 0x07U;
            continuation_count = 3U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (index + continuation_count >= length) {
            return false;
        }
        for (size_t offset = 1U; offset <= continuation_count; ++offset) {
            const uint8_t next = (uint8_t)value[index + offset];
            if ((next & 0xC0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (next & 0x3FU);
        }
        if (codepoint < minimum || codepoint > 0x10FFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            return false;
        }
        index += continuation_count + 1U;
    }
    return true;
}

static int token_lex_start(const jsmntok_t *token)
{
    return (JSMN_STRING == token->type) ? token->start - 1 : token->start;
}

static int token_lex_end(const jsmntok_t *token)
{
    return (JSMN_STRING == token->type) ? token->end + 1 : token->end;
}

static int skip_whitespace(const parse_context_t *context, int position)
{
    while (position < (int)context->length &&
           is_whitespace(context->json[position])) {
        position++;
    }
    return position;
}

static bool validate_container_syntax(parse_context_t *context, int container_index)
{
    const jsmntok_t *container = &context->tokens[container_index];
    int cursor = skip_whitespace(context, container->start + 1);
    int child_count = 0;

    for (int index = container_index + 1; index < context->token_count; ++index) {
        const jsmntok_t *child = &context->tokens[index];
        int value_index = index;

        if (child->parent != container_index) {
            continue;
        }
        if (0 < child_count) {
            if (cursor >= container->end - 1 || ',' != context->json[cursor]) {
                return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
            }
            cursor = skip_whitespace(context, cursor + 1);
        }
        if (JSMN_OBJECT == container->type) {
            if (JSMN_STRING != child->type || index + 1 >= context->token_count ||
                context->tokens[index + 1].parent != index) {
                return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
            }
            if (cursor != token_lex_start(child)) {
                return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
            }
            cursor = skip_whitespace(context, token_lex_end(child));
            if (cursor >= container->end - 1 || ':' != context->json[cursor]) {
                return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
            }
            cursor = skip_whitespace(context, cursor + 1);
            value_index = index + 1;
        }
        if (cursor != token_lex_start(&context->tokens[value_index])) {
            return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
        }
        cursor = skip_whitespace(context, token_lex_end(&context->tokens[value_index]));
        child_count++;
    }
    if (cursor != container->end - 1 || child_count != container->size) {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
    }
    return true;
}

static bool validate_token_tree(parse_context_t *context)
{
    if (context->token_count <= 0 || JSMN_OBJECT != context->tokens[0].type ||
        -1 != context->tokens[0].parent) {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
    }
    for (int index = 0; index < context->token_count; ++index) {
        const jsmntok_t *token = &context->tokens[index];
        if (token->start < 0 || token->end <= token->start ||
            token->end > (int)context->length) {
            return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
        }
        if (0 < index) {
            if (token->parent < 0 || token->parent >= index) {
                return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
            }
            const jsmntok_t *parent = &context->tokens[token->parent];
            if (JSMN_OBJECT == parent->type && JSMN_STRING != token->type) {
                return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
            }
            if (JSMN_STRING == parent->type &&
                (parent->parent < 0 ||
                 JSMN_OBJECT != context->tokens[parent->parent].type ||
                 1 != parent->size || token->parent + 1 != index)) {
                return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
            }
            if (JSMN_PRIMITIVE == parent->type) {
                return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
            }
        }
        if (JSMN_STRING == token->type && token->parent >= 0 &&
            JSMN_OBJECT == context->tokens[token->parent].type &&
            (1 != token->size || index + 1 >= context->token_count ||
             context->tokens[index + 1].parent != index)) {
            return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
        }
        if ((JSMN_OBJECT == token->type || JSMN_ARRAY == token->type) &&
            !validate_container_syntax(context, index)) {
            return false;
        }
    }

    int root_start = skip_whitespace(context, 0);
    int root_end = skip_whitespace(context, context->tokens[0].end);
    if (root_start != context->tokens[0].start || root_end != (int)context->length) {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
    }
    return true;
}

static bool token_equals_ascii(const parse_context_t *context, int token_index,
                               const char *expected)
{
    const jsmntok_t *token = &context->tokens[token_index];
    size_t expected_length = strlen(expected);

    return JSMN_STRING == token->type &&
           (size_t)(token->end - token->start) == expected_length &&
           0 == memcmp(context->json + token->start, expected, expected_length);
}

static int object_get(parse_context_t *context, int object_index,
                      const char *key, bool required)
{
    int found = -1;

    if (object_index < 0 || object_index >= context->token_count ||
        JSMN_OBJECT != context->tokens[object_index].type) {
        fail(context, NOTIFIER_PROTOCOL_ERROR_TYPE);
        return -1;
    }
    for (int index = object_index + 1; index < context->token_count; ++index) {
        if (context->tokens[index].parent != object_index ||
            !token_equals_ascii(context, index, key)) {
            continue;
        }
        if (-1 != found) {
            fail(context, NOTIFIER_PROTOCOL_ERROR_DUPLICATE);
            return -1;
        }
        found = index + 1;
    }
    if (required && -1 == found) {
        fail(context, NOTIFIER_PROTOCOL_ERROR_MISSING);
    }
    return found;
}

static bool parse_uint(parse_context_t *context, int token_index,
                       uint64_t maximum, uint64_t *result)
{
    const jsmntok_t *token = NULL;
    uint64_t value = 0U;

    if (token_index < 0 || token_index >= context->token_count || NULL == result) {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_TYPE);
    }
    token = &context->tokens[token_index];
    if (JSMN_PRIMITIVE != token->type || token->start == token->end) {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_TYPE);
    }
    if ('0' == context->json[token->start] && token->end - token->start > 1) {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_TYPE);
    }
    for (int position = token->start; position < token->end; ++position) {
        const char digit = context->json[position];
        if (digit < '0' || digit > '9') {
            return fail(context, NOTIFIER_PROTOCOL_ERROR_TYPE);
        }
        if (value > (maximum - (uint64_t)(digit - '0')) / 10U) {
            return fail(context, NOTIFIER_PROTOCOL_ERROR_RANGE);
        }
        value = (value * 10U) + (uint64_t)(digit - '0');
    }
    *result = value;
    return true;
}

static bool parse_boolean(parse_context_t *context, int token_index, bool *result)
{
    const jsmntok_t *token = NULL;
    size_t length = 0U;

    if (token_index < 0 || token_index >= context->token_count || NULL == result) {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_TYPE);
    }
    token = &context->tokens[token_index];
    length = (size_t)(token->end - token->start);
    if (JSMN_PRIMITIVE != token->type) {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_TYPE);
    }
    if (4U == length && 0 == memcmp(context->json + token->start, "true", 4U)) {
        *result = true;
        return true;
    }
    if (5U == length && 0 == memcmp(context->json + token->start, "false", 5U)) {
        *result = false;
        return true;
    }
    return fail(context, NOTIFIER_PROTOCOL_ERROR_TYPE);
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool append_codepoint(char *output, size_t capacity, size_t *length,
                             uint32_t codepoint)
{
    uint8_t encoded[4] = {0};
    size_t encoded_length = 0U;

    if (codepoint < 0x20U || 0x7FU == codepoint ||
        codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
        return false;
    }
    if (codepoint < 0x80U) {
        encoded[0] = (uint8_t)codepoint;
        encoded_length = 1U;
    } else if (codepoint < 0x800U) {
        encoded[0] = (uint8_t)(0xC0U | (codepoint >> 6U));
        encoded[1] = (uint8_t)(0x80U | (codepoint & 0x3FU));
        encoded_length = 2U;
    } else if (codepoint < 0x10000U) {
        encoded[0] = (uint8_t)(0xE0U | (codepoint >> 12U));
        encoded[1] = (uint8_t)(0x80U | ((codepoint >> 6U) & 0x3FU));
        encoded[2] = (uint8_t)(0x80U | (codepoint & 0x3FU));
        encoded_length = 3U;
    } else {
        encoded[0] = (uint8_t)(0xF0U | (codepoint >> 18U));
        encoded[1] = (uint8_t)(0x80U | ((codepoint >> 12U) & 0x3FU));
        encoded[2] = (uint8_t)(0x80U | ((codepoint >> 6U) & 0x3FU));
        encoded[3] = (uint8_t)(0x80U | (codepoint & 0x3FU));
        encoded_length = 4U;
    }
    if (*length + encoded_length >= capacity) {
        return false;
    }
    memcpy(output + *length, encoded, encoded_length);
    *length += encoded_length;
    return true;
}

static bool parse_hex4(const parse_context_t *context, int position,
                       uint32_t *codepoint)
{
    uint32_t value = 0U;

    if (position + 4 > (int)context->length || NULL == codepoint) {
        return false;
    }
    for (int offset = 0; offset < 4; ++offset) {
        int digit = hex_value(context->json[position + offset]);
        if (digit < 0) {
            return false;
        }
        value = (value << 4U) | (uint32_t)digit;
    }
    *codepoint = value;
    return true;
}

static bool parse_string(parse_context_t *context, int token_index,
                         char *output, size_t capacity, bool ascii_only)
{
    const jsmntok_t *token = NULL;
    size_t output_length = 0U;

    if (token_index < 0 || token_index >= context->token_count ||
        NULL == output || capacity < 2U) {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_TYPE);
    }
    token = &context->tokens[token_index];
    if (JSMN_STRING != token->type) {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_TYPE);
    }
    for (int position = token->start; position < token->end; ++position) {
        uint8_t value = (uint8_t)context->json[position];
        if ('\\' != value) {
            if (value < 0x20U || 0x7FU == value ||
                (ascii_only && value > 0x7EU) || output_length + 1U >= capacity) {
                return fail(context, NOTIFIER_PROTOCOL_ERROR_RANGE);
            }
            output[output_length++] = (char)value;
            continue;
        }

        position++;
        if (position >= token->end) {
            return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
        }
        value = (uint8_t)context->json[position];
        if ('"' == value || '\\' == value || '/' == value) {
            if (output_length + 1U >= capacity) {
                return fail(context, NOTIFIER_PROTOCOL_ERROR_RANGE);
            }
            output[output_length++] = (char)value;
            continue;
        }
        if ('u' != value) {
            return fail(context, NOTIFIER_PROTOCOL_ERROR_RANGE);
        }

        uint32_t codepoint = 0U;
        if (!parse_hex4(context, position + 1, &codepoint)) {
            return fail(context, NOTIFIER_PROTOCOL_ERROR_JSON);
        }
        position += 4;
        if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
            uint32_t low = 0U;
            if (position + 6 >= token->end ||
                '\\' != context->json[position + 1] ||
                'u' != context->json[position + 2] ||
                !parse_hex4(context, position + 3, &low) ||
                low < 0xDC00U || low > 0xDFFFU) {
                return fail(context, NOTIFIER_PROTOCOL_ERROR_UTF8);
            }
            codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) +
                        (low - 0xDC00U);
            position += 6;
        } else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU) {
            return fail(context, NOTIFIER_PROTOCOL_ERROR_UTF8);
        }
        if (ascii_only && codepoint > 0x7EU) {
            return fail(context, NOTIFIER_PROTOCOL_ERROR_RANGE);
        }
        if (!append_codepoint(output, capacity, &output_length, codepoint)) {
            return fail(context, NOTIFIER_PROTOCOL_ERROR_RANGE);
        }
    }
    if (0U == output_length) {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_RANGE);
    }
    output[output_length] = '\0';
    return true;
}

static int array_children(parse_context_t *context, int array_index,
                          int *children, size_t capacity)
{
    int count = 0;

    if (array_index < 0 || array_index >= context->token_count ||
        JSMN_ARRAY != context->tokens[array_index].type) {
        fail(context, NOTIFIER_PROTOCOL_ERROR_TYPE);
        return -1;
    }
    for (int index = array_index + 1; index < context->token_count; ++index) {
        if (context->tokens[index].parent != array_index) {
            continue;
        }
        if ((size_t)count >= capacity) {
            fail(context, NOTIFIER_PROTOCOL_ERROR_RANGE);
            return -1;
        }
        children[count++] = index;
    }
    return count;
}

static bool parse_aggregate(parse_context_t *context, int object_index,
                            notifier_snapshot_t *snapshot)
{
    char state[FIELD_TEXT_MAX_BYTES + 1U] = {0};
    uint64_t value = 0U;

    int state_index = object_get(context, object_index, "state", true);
    if (state_index < 0 || !parse_string(context, state_index, state, sizeof(state), true)) {
        return false;
    }
    if (0 == strcmp(state, "IDLE")) {
        snapshot->aggregate_state = NOTIFIER_AGGREGATE_IDLE;
    } else if (0 == strcmp(state, "RUNNING")) {
        snapshot->aggregate_state = NOTIFIER_AGGREGATE_RUNNING;
    } else {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_ENUM);
    }

#define PARSE_COUNT(field_name, member_name)                                      \
    do {                                                                           \
        int field_index = object_get(context, object_index, field_name, true);      \
        if (field_index < 0 || !parse_uint(context, field_index, UINT16_MAX, &value)) { \
            return false;                                                          \
        }                                                                          \
        snapshot->member_name = (uint16_t)value;                                    \
    } while (0)

    PARSE_COUNT("total_count", total_count);
    PARSE_COUNT("running_count", running_count);
    PARSE_COUNT("done_count", done_count);
    PARSE_COUNT("stop_count", stop_count);
    PARSE_COUNT("overflow_count", overflow_count);
#undef PARSE_COUNT

    if (snapshot->running_count > snapshot->total_count ||
        snapshot->done_count > snapshot->total_count ||
        snapshot->stop_count > snapshot->total_count ||
        snapshot->overflow_count > snapshot->total_count ||
        (uint32_t)snapshot->running_count + snapshot->done_count +
                snapshot->stop_count > snapshot->total_count ||
        ((0U < snapshot->running_count) !=
         (NOTIFIER_AGGREGATE_RUNNING == snapshot->aggregate_state))) {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_RELATION);
    }
    return true;
}

static bool parse_task(parse_context_t *context, int object_index,
                       notifier_task_t *task)
{
    char field[FIELD_TEXT_MAX_BYTES + 1U] = {0};
    uint64_t number = 0U;
    int value_index = object_get(context, object_index, "id", true);

    if (value_index < 0 ||
        !parse_string(context, value_index, task->id, sizeof(task->id), true)) {
        return false;
    }
    value_index = object_get(context, object_index, "surface", true);
    if (value_index < 0 ||
        !parse_string(context, value_index, field, sizeof(field), true)) {
        return false;
    }
    if (0 == strcmp(field, "APP")) {
        task->surface = NOTIFIER_SURFACE_APP;
    } else if (0 == strcmp(field, "VS")) {
        task->surface = NOTIFIER_SURFACE_VS;
    } else if (0 == strcmp(field, "CODEX")) {
        task->surface = NOTIFIER_SURFACE_CODEX;
    } else {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_ENUM);
    }
    value_index = object_get(context, object_index, "project", true);
    if (value_index < 0 ||
        !parse_string(context, value_index, task->project, sizeof(task->project), false)) {
        return false;
    }
    value_index = object_get(context, object_index, "title", false);
    if (value_index < 0) {
        if (NOTIFIER_PROTOCOL_OK != *context->error) {
            return false;
        }
        memcpy(task->title, task->project, sizeof(task->project));
    } else if (!parse_string(context, value_index, task->title,
                             sizeof(task->title), false)) {
        return false;
    }
    memset(field, 0, sizeof(field));
    value_index = object_get(context, object_index, "status", true);
    if (value_index < 0 ||
        !parse_string(context, value_index, field, sizeof(field), true)) {
        return false;
    }
    if (0 == strcmp(field, "RUN")) {
        task->status = NOTIFIER_TASK_RUN;
    } else if (0 == strcmp(field, "DONE")) {
        task->status = NOTIFIER_TASK_DONE;
    } else if (0 == strcmp(field, "STOP")) {
        task->status = NOTIFIER_TASK_STOP;
    } else if (0 == strcmp(field, "UNKNOWN")) {
        task->status = NOTIFIER_TASK_UNKNOWN;
    } else {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_ENUM);
    }
    value_index = object_get(context, object_index, "elapsed_ms", true);
    if (value_index < 0 || !parse_uint(context, value_index, UINT32_MAX, &number)) {
        return false;
    }
    task->elapsed_ms = (uint32_t)number;
    value_index = object_get(context, object_index, "updated_at_ms", true);
    return value_index >= 0 && parse_uint(context, value_index, UINT64_MAX,
                                          &task->updated_at_ms);
}

static bool parse_event(parse_context_t *context, int object_index,
                        notifier_event_t *event)
{
    char field[FIELD_TEXT_MAX_BYTES + 1U] = {0};
    int value_index = object_get(context, object_index, "seq", true);

    if (value_index < 0 || !parse_uint(context, value_index, UINT64_MAX, &event->seq) ||
        0U == event->seq) {
        if (value_index >= 0 && NOTIFIER_PROTOCOL_OK == *context->error) {
            fail(context, NOTIFIER_PROTOCOL_ERROR_RANGE);
        }
        return false;
    }
    value_index = object_get(context, object_index, "task_id", true);
    if (value_index < 0 ||
        !parse_string(context, value_index, event->task_id, sizeof(event->task_id), true)) {
        return false;
    }
    value_index = object_get(context, object_index, "type", true);
    if (value_index < 0 ||
        !parse_string(context, value_index, field, sizeof(field), true)) {
        return false;
    }
    if (0 == strcmp(field, "TURN_COMPLETED")) {
        event->type = NOTIFIER_EVENT_TURN_COMPLETED;
    } else if (0 == strcmp(field, "TURN_STOPPED")) {
        event->type = NOTIFIER_EVENT_TURN_STOPPED;
    } else {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_ENUM);
    }
    value_index = object_get(context, object_index, "notify", true);
    if (value_index < 0 || !parse_boolean(context, value_index, &event->notify)) {
        return false;
    }
    if (NOTIFIER_EVENT_TURN_STOPPED == event->type && event->notify) {
        return fail(context, NOTIFIER_PROTOCOL_ERROR_RELATION);
    }
    value_index = object_get(context, object_index, "occurred_at_ms", true);
    return value_index >= 0 && parse_uint(context, value_index, UINT64_MAX,
                                          &event->occurred_at_ms);
}

bool notifier_protocol_parse(const char *json, size_t length,
                             notifier_snapshot_t *snapshot,
                             notifier_protocol_error_t *error)
{
    notifier_snapshot_t *parsed = NULL;
    jsmntok_t *tokens = NULL;
    jsmn_parser parser = {0};
    parse_context_t context = {0};
    int token_count = 0;
    uint64_t number = 0U;
    bool success = false;

    if (NULL != error) {
        *error = NOTIFIER_PROTOCOL_OK;
    }
    if (NULL == json || NULL == snapshot || NULL == error) {
        if (NULL != error) {
            *error = NOTIFIER_PROTOCOL_ERROR_ARGUMENT;
        }
        return false;
    }
    if (0U == length || length > NOTIFIER_PROTOCOL_MAX_RESPONSE_BYTES) {
        *error = NOTIFIER_PROTOCOL_ERROR_SIZE;
        return false;
    }
    if (!validate_utf8(json, length)) {
        *error = NOTIFIER_PROTOCOL_ERROR_UTF8;
        return false;
    }

    tokens = calloc(NOTIFIER_JSON_MAX_TOKENS, sizeof(*tokens));
    parsed = calloc(1U, sizeof(*parsed));
    if (NULL == tokens || NULL == parsed) {
        *error = NOTIFIER_PROTOCOL_ERROR_MEMORY;
        goto cleanup;
    }
    jsmn_init(&parser);
    token_count = jsmn_parse(&parser, json, length, tokens, NOTIFIER_JSON_MAX_TOKENS);
    context.json = json;
    context.length = length;
    context.tokens = tokens;
    context.token_count = token_count;
    context.error = error;
    if (token_count < 0) {
        fail(&context, (JSMN_ERROR_NOMEM == token_count)
                           ? NOTIFIER_PROTOCOL_ERROR_SIZE
                           : NOTIFIER_PROTOCOL_ERROR_JSON);
        goto cleanup;
    }
    if (!validate_token_tree(&context)) {
        goto cleanup;
    }

    int value_index = object_get(&context, 0, "schema_version", true);
    if (value_index < 0 || !parse_uint(&context, value_index, UINT8_MAX, &number) ||
        1U != number) {
        if (NOTIFIER_PROTOCOL_OK == *error) {
            fail(&context, NOTIFIER_PROTOCOL_ERROR_RANGE);
        }
        goto cleanup;
    }
    parsed->schema_version = 1U;
    value_index = object_get(&context, 0, "generated_at_ms", true);
    if (value_index < 0 ||
        !parse_uint(&context, value_index, UINT64_MAX, &parsed->generated_at_ms)) {
        goto cleanup;
    }
    char bridge_status[FIELD_TEXT_MAX_BYTES + 1U] = {0};
    value_index = object_get(&context, 0, "bridge_status", true);
    if (value_index < 0 ||
        !parse_string(&context, value_index, bridge_status, sizeof(bridge_status), true)) {
        goto cleanup;
    }
    if (0 != strcmp(bridge_status, "ONLINE")) {
        fail(&context, NOTIFIER_PROTOCOL_ERROR_ENUM);
        goto cleanup;
    }
    value_index = object_get(&context, 0, "aggregate", true);
    if (value_index < 0 || !parse_aggregate(&context, value_index, parsed)) {
        goto cleanup;
    }

    int task_indices[NOTIFIER_MAX_TASKS] = {0};
    value_index = object_get(&context, 0, "tasks", true);
    int task_count = (value_index >= 0)
                         ? array_children(&context, value_index, task_indices,
                                          NOTIFIER_MAX_TASKS)
                         : -1;
    if (task_count < 0) {
        goto cleanup;
    }
    parsed->task_count = (uint8_t)task_count;
    for (uint8_t index = 0U; index < parsed->task_count; ++index) {
        if (JSMN_OBJECT != tokens[task_indices[index]].type ||
            !parse_task(&context, task_indices[index], &parsed->tasks[index])) {
            if (NOTIFIER_PROTOCOL_OK == *error) {
                fail(&context, NOTIFIER_PROTOCOL_ERROR_TYPE);
            }
            goto cleanup;
        }
        for (uint8_t previous = 0U; previous < index; ++previous) {
            if (0 == strcmp(parsed->tasks[previous].id,
                            parsed->tasks[index].id)) {
                fail(&context, NOTIFIER_PROTOCOL_ERROR_DUPLICATE);
                goto cleanup;
            }
        }
    }
    if ((uint32_t)parsed->task_count + parsed->overflow_count !=
        parsed->total_count) {
        fail(&context, NOTIFIER_PROTOCOL_ERROR_RELATION);
        goto cleanup;
    }

    uint16_t visible_running = 0U;
    uint16_t visible_done = 0U;
    uint16_t visible_stop = 0U;
    for (uint8_t index = 0U; index < parsed->task_count; ++index) {
        visible_running +=
            (NOTIFIER_TASK_RUN == parsed->tasks[index].status) ? 1U : 0U;
        visible_done +=
            (NOTIFIER_TASK_DONE == parsed->tasks[index].status) ? 1U : 0U;
        visible_stop +=
            (NOTIFIER_TASK_STOP == parsed->tasks[index].status) ? 1U : 0U;
    }
    if (visible_running > parsed->running_count ||
        visible_done > parsed->done_count ||
        visible_stop > parsed->stop_count) {
        fail(&context, NOTIFIER_PROTOCOL_ERROR_RELATION);
        goto cleanup;
    }

    int event_indices[NOTIFIER_MAX_EVENTS] = {0};
    value_index = object_get(&context, 0, "events", true);
    int event_count = (value_index >= 0)
                          ? array_children(&context, value_index, event_indices,
                                           NOTIFIER_MAX_EVENTS)
                          : -1;
    if (event_count < 0) {
        goto cleanup;
    }
    parsed->event_count = (uint8_t)event_count;
    for (uint8_t index = 0U; index < parsed->event_count; ++index) {
        if (JSMN_OBJECT != tokens[event_indices[index]].type ||
            !parse_event(&context, event_indices[index],
                         &parsed->events[index])) {
            if (NOTIFIER_PROTOCOL_OK == *error) {
                fail(&context, NOTIFIER_PROTOCOL_ERROR_TYPE);
            }
            goto cleanup;
        }
        if (0U < index &&
            parsed->events[index].seq <= parsed->events[index - 1U].seq) {
            fail(&context, NOTIFIER_PROTOCOL_ERROR_RELATION);
            goto cleanup;
        }
    }

    value_index = object_get(&context, 0, "events_truncated", false);
    if (value_index >= 0 &&
        !parse_boolean(&context, value_index, &parsed->events_truncated)) {
        goto cleanup;
    }

    *snapshot = *parsed;
    success = true;

cleanup:
    free(tokens);
    free(parsed);
    return success;
}

const char *notifier_protocol_error_name(notifier_protocol_error_t error)
{
    switch (error) {
        case NOTIFIER_PROTOCOL_OK:
            return "OK";
        case NOTIFIER_PROTOCOL_ERROR_ARGUMENT:
            return "ARGUMENT";
        case NOTIFIER_PROTOCOL_ERROR_SIZE:
            return "SIZE";
        case NOTIFIER_PROTOCOL_ERROR_MEMORY:
            return "MEMORY";
        case NOTIFIER_PROTOCOL_ERROR_JSON:
            return "JSON";
        case NOTIFIER_PROTOCOL_ERROR_UTF8:
            return "UTF8";
        case NOTIFIER_PROTOCOL_ERROR_MISSING:
            return "MISSING";
        case NOTIFIER_PROTOCOL_ERROR_TYPE:
            return "TYPE";
        case NOTIFIER_PROTOCOL_ERROR_RANGE:
            return "RANGE";
        case NOTIFIER_PROTOCOL_ERROR_ENUM:
            return "ENUM";
        case NOTIFIER_PROTOCOL_ERROR_DUPLICATE:
            return "DUPLICATE";
        case NOTIFIER_PROTOCOL_ERROR_RELATION:
            return "RELATION";
        default:
            return "UNKNOWN";
    }
}
