#pragma once

#include "notifier_model.h"

#include <stdbool.h>
#include <stddef.h>

#define NOTIFIER_PROTOCOL_MAX_RESPONSE_BYTES 12288U

typedef enum {
    NOTIFIER_PROTOCOL_OK = 0,
    NOTIFIER_PROTOCOL_ERROR_ARGUMENT,
    NOTIFIER_PROTOCOL_ERROR_SIZE,
    NOTIFIER_PROTOCOL_ERROR_MEMORY,
    NOTIFIER_PROTOCOL_ERROR_JSON,
    NOTIFIER_PROTOCOL_ERROR_UTF8,
    NOTIFIER_PROTOCOL_ERROR_MISSING,
    NOTIFIER_PROTOCOL_ERROR_TYPE,
    NOTIFIER_PROTOCOL_ERROR_RANGE,
    NOTIFIER_PROTOCOL_ERROR_ENUM,
    NOTIFIER_PROTOCOL_ERROR_DUPLICATE,
    NOTIFIER_PROTOCOL_ERROR_RELATION,
} notifier_protocol_error_t;

bool notifier_protocol_parse(const char *json, size_t length,
                             notifier_snapshot_t *snapshot,
                             notifier_protocol_error_t *error);
const char *notifier_protocol_error_name(notifier_protocol_error_t error);
