#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HOLO_LOGICAL_WIDTH 240U
#define HOLO_LOGICAL_HEIGHT 240U
#define HOLO_CONTENT_WIDTH 240U
#define HOLO_CONTENT_HEIGHT 240U
#define HOLO_PAGE_COUNT 4U
#define HOLO_SPECTRUM_MODE_COUNT 6U
#define HOLO_INPUT_DEDUP_MS 300U

typedef enum {
    HOLO_PAGE_ANIMATION = 0,
    HOLO_PAGE_WEATHER,
    HOLO_PAGE_CLOCK,
    HOLO_PAGE_SPECTRUM,
} holocubic_page_t;

typedef enum {
    HOLO_COMMAND_PREVIOUS = 0,
    HOLO_COMMAND_NEXT,
    HOLO_COMMAND_CONFIRM,
} holocubic_command_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} holocubic_rect_t;

typedef struct {
    holocubic_page_t page;
    uint8_t spectrum_mode;
    uint32_t revision;
    uint32_t last_command_ms;
    holocubic_command_t last_command;
} holocubic_model_t;

holocubic_rect_t holocubic_layout(uint16_t physical_width,
                                  uint16_t physical_height);
uint16_t holocubic_scale_coordinate(uint16_t destination_coordinate,
                                    uint16_t destination_extent,
                                    uint16_t source_extent);
void holocubic_model_init(holocubic_model_t *model);
bool holocubic_model_dispatch(holocubic_model_t *model,
                               holocubic_command_t command,
                               uint32_t now_ms);
