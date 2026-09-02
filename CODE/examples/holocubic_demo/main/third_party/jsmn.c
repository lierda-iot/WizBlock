/*
 * jsmn (Minimalistic JSON parser in C)
 * Copyright (c) 2010 Serge A. Zaitsev
 * SPDX-License-Identifier: MIT
 */

#include "jsmn.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static jsmntok_t *alloc_token(jsmn_parser *parser, jsmntok_t *tokens,
                              size_t token_count)
{
    jsmntok_t *token = NULL;

    if (parser->toknext >= token_count) {
        return NULL;
    }
    token = &tokens[parser->toknext++];
    token->start = -1;
    token->end = -1;
    token->size = 0;
    token->parent = -1;
    token->type = JSMN_UNDEFINED;
    return token;
}

static void fill_token(jsmntok_t *token, jsmntype_t type, int start, int end)
{
    token->type = type;
    token->start = start;
    token->end = end;
    token->size = 0;
}

static bool is_hex(char value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static bool valid_number(const char *value, size_t length)
{
    size_t index = 0U;

    if (0U == length) {
        return false;
    }
    if ('-' == value[index]) {
        index++;
        if (index == length) {
            return false;
        }
    }
    if ('0' == value[index]) {
        index++;
    } else {
        if (value[index] < '1' || value[index] > '9') {
            return false;
        }
        do {
            index++;
        } while (index < length && value[index] >= '0' && value[index] <= '9');
    }
    if (index < length && '.' == value[index]) {
        index++;
        if (index == length || value[index] < '0' || value[index] > '9') {
            return false;
        }
        do {
            index++;
        } while (index < length && value[index] >= '0' && value[index] <= '9');
    }
    if (index < length && ('e' == value[index] || 'E' == value[index])) {
        index++;
        if (index < length && ('+' == value[index] || '-' == value[index])) {
            index++;
        }
        if (index == length || value[index] < '0' || value[index] > '9') {
            return false;
        }
        do {
            index++;
        } while (index < length && value[index] >= '0' && value[index] <= '9');
    }
    return index == length;
}

static bool valid_primitive(const char *value, size_t length)
{
    if ((4U == length && 0 == memcmp(value, "true", 4U)) ||
        (5U == length && 0 == memcmp(value, "false", 5U)) ||
        (4U == length && 0 == memcmp(value, "null", 4U))) {
        return true;
    }
    return valid_number(value, length);
}

static int parse_primitive(jsmn_parser *parser, const char *json, size_t length,
                           jsmntok_t *tokens, size_t token_count)
{
    unsigned int start = parser->pos;
    jsmntok_t *token = NULL;

    for (; parser->pos < length; parser->pos++) {
        char value = json[parser->pos];
        if ('\t' == value || '\r' == value || '\n' == value || ' ' == value ||
            ',' == value || ']' == value || '}' == value) {
            break;
        }
        if ((unsigned char)value < 0x20U || '"' == value || ':' == value ||
            '[' == value || '{' == value) {
            parser->pos = start;
            return JSMN_ERROR_INVAL;
        }
    }
    if (!valid_primitive(json + start, parser->pos - start)) {
        parser->pos = start;
        return JSMN_ERROR_INVAL;
    }
    token = alloc_token(parser, tokens, token_count);
    if (NULL == token) {
        parser->pos = start;
        return JSMN_ERROR_NOMEM;
    }
    fill_token(token, JSMN_PRIMITIVE, (int)start, (int)parser->pos);
    token->parent = parser->toksuper;
    if (parser->pos < length) {
        parser->pos--;
    }
    return 0;
}

static int parse_string(jsmn_parser *parser, const char *json, size_t length,
                        jsmntok_t *tokens, size_t token_count)
{
    unsigned int start = parser->pos;

    parser->pos++;
    for (; parser->pos < length; parser->pos++) {
        char value = json[parser->pos];
        if ('"' == value) {
            jsmntok_t *token = alloc_token(parser, tokens, token_count);
            if (NULL == token) {
                parser->pos = start;
                return JSMN_ERROR_NOMEM;
            }
            fill_token(token, JSMN_STRING, (int)start + 1, (int)parser->pos);
            token->parent = parser->toksuper;
            return 0;
        }
        if ((unsigned char)value < 0x20U) {
            parser->pos = start;
            return JSMN_ERROR_INVAL;
        }
        if ('\\' == value) {
            parser->pos++;
            if (parser->pos >= length) {
                parser->pos = start;
                return JSMN_ERROR_PART;
            }
            value = json[parser->pos];
            if ('"' == value || '/' == value || '\\' == value ||
                'b' == value || 'f' == value || 'r' == value ||
                'n' == value || 't' == value) {
                continue;
            }
            if ('u' == value) {
                for (unsigned int index = 0U; index < 4U; ++index) {
                    parser->pos++;
                    if (parser->pos >= length || !is_hex(json[parser->pos])) {
                        parser->pos = start;
                        return JSMN_ERROR_INVAL;
                    }
                }
                continue;
            }
            parser->pos = start;
            return JSMN_ERROR_INVAL;
        }
    }
    parser->pos = start;
    return JSMN_ERROR_PART;
}

void jsmn_init(jsmn_parser *parser)
{
    if (NULL == parser) {
        return;
    }
    parser->pos = 0U;
    parser->toknext = 0U;
    parser->toksuper = -1;
}

int jsmn_parse(jsmn_parser *parser, const char *json, size_t length,
               jsmntok_t *tokens, unsigned int token_count)
{
    int result = 0;
    int index = 0;

    if (NULL == parser || NULL == json || NULL == tokens) {
        return JSMN_ERROR_INVAL;
    }
    for (; parser->pos < length; parser->pos++) {
        char value = json[parser->pos];
        jsmntok_t *token = NULL;
        switch (value) {
            case '{':
            case '[':
                token = alloc_token(parser, tokens, token_count);
                if (NULL == token) {
                    return JSMN_ERROR_NOMEM;
                }
                if (-1 != parser->toksuper) {
                    tokens[parser->toksuper].size++;
                    token->parent = parser->toksuper;
                }
                fill_token(token, ('{' == value) ? JSMN_OBJECT : JSMN_ARRAY,
                           (int)parser->pos, -1);
                parser->toksuper = (int)parser->toknext - 1;
                break;
            case '}':
            case ']': {
                jsmntype_t expected = ('}' == value) ? JSMN_OBJECT : JSMN_ARRAY;
                for (index = (int)parser->toknext - 1; index >= 0; --index) {
                    token = &tokens[index];
                    if (-1 == token->start || -1 != token->end) {
                        continue;
                    }
                    if (token->type != expected) {
                        return JSMN_ERROR_INVAL;
                    }
                    token->end = (int)parser->pos + 1;
                    parser->toksuper = token->parent;
                    break;
                }
                if (index < 0) {
                    return JSMN_ERROR_INVAL;
                }
                break;
            }
            case '"':
                result = parse_string(parser, json, length, tokens, token_count);
                if (result < 0) {
                    return result;
                }
                if (-1 != parser->toksuper) {
                    tokens[parser->toksuper].size++;
                }
                break;
            case '\t':
            case '\r':
            case '\n':
            case ' ':
                break;
            case ':':
                if (0U == parser->toknext ||
                    JSMN_STRING != tokens[parser->toknext - 1U].type) {
                    return JSMN_ERROR_INVAL;
                }
                parser->toksuper = (int)parser->toknext - 1;
                break;
            case ',':
                if (-1 != parser->toksuper &&
                    JSMN_ARRAY != tokens[parser->toksuper].type &&
                    JSMN_OBJECT != tokens[parser->toksuper].type) {
                    parser->toksuper = tokens[parser->toksuper].parent;
                }
                break;
            default:
                result = parse_primitive(parser, json, length, tokens, token_count);
                if (result < 0) {
                    return result;
                }
                if (-1 != parser->toksuper) {
                    tokens[parser->toksuper].size++;
                }
                break;
        }
    }
    for (index = (int)parser->toknext - 1; index >= 0; --index) {
        if (-1 != tokens[index].start && -1 == tokens[index].end) {
            return JSMN_ERROR_PART;
        }
    }
    return (int)parser->toknext;
}
