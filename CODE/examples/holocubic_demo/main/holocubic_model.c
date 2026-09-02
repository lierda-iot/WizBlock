#include "holocubic_model.h"

#include <stddef.h>

static holocubic_page_t previous_page(holocubic_page_t page)
{
    return (HOLO_PAGE_ANIMATION == page) ? (holocubic_page_t)(HOLO_PAGE_COUNT - 1U)
                                         : (holocubic_page_t)(page - 1U);
}

static holocubic_page_t next_page(holocubic_page_t page)
{
    return (holocubic_page_t)((page + 1U) % HOLO_PAGE_COUNT);
}

holocubic_rect_t holocubic_layout(uint16_t physical_width,
                                  uint16_t physical_height)
{
    holocubic_rect_t rect = {
        .x = 0U,
        .y = 0U,
        .width = HOLO_CONTENT_WIDTH,
        .height = HOLO_CONTENT_HEIGHT,
    };

    rect.width = (physical_width < rect.width) ? physical_width : rect.width;
    rect.height = (physical_height < rect.height) ? physical_height : rect.height;
    rect.x = (uint16_t)((physical_width - rect.width) / 2U);
    rect.y = (uint16_t)((physical_height - rect.height) / 2U);
    return rect;
}

uint16_t holocubic_scale_coordinate(uint16_t destination_coordinate,
                                    uint16_t destination_extent,
                                    uint16_t source_extent)
{
    uint32_t source_coordinate = 0U;

    if (0U == destination_extent || 0U == source_extent ||
        destination_coordinate >= destination_extent) {
        return 0U;
    }
    source_coordinate =
        ((uint32_t)(2U * destination_coordinate + 1U) * source_extent) /
        (2U * destination_extent);
    return (uint16_t)((source_coordinate < source_extent) ?
                      source_coordinate : source_extent - 1U);
}

void holocubic_model_init(holocubic_model_t *model)
{
    if (NULL == model) {
        return;
    }
    *model = (holocubic_model_t){
        .page = HOLO_PAGE_ANIMATION,
        .spectrum_mode = 0U,
        .revision = 1U,
        .last_command_ms = 0U,
        .last_command = HOLO_COMMAND_CONFIRM,
    };
}

bool holocubic_model_dispatch(holocubic_model_t *model,
                              holocubic_command_t command,
                              uint32_t now_ms)
{
    holocubic_page_t next = HOLO_PAGE_ANIMATION;

    if (NULL == model || command > HOLO_COMMAND_CONFIRM) {
        return false;
    }
    if (model->revision > 1U &&
        (uint32_t)(now_ms - model->last_command_ms) < HOLO_INPUT_DEDUP_MS &&
        command == model->last_command) {
        return false;
    }
    next = model->page;
    if (HOLO_COMMAND_PREVIOUS == command) {
        next = previous_page(model->page);
    } else if (HOLO_COMMAND_NEXT == command) {
        next = next_page(model->page);
    } else if (HOLO_COMMAND_CONFIRM == command &&
               HOLO_PAGE_SPECTRUM == model->page) {
        model->spectrum_mode = (uint8_t)((model->spectrum_mode + 1U) %
                                         HOLO_SPECTRUM_MODE_COUNT);
    }
    if (next == model->page && HOLO_COMMAND_CONFIRM != command) {
        return false;
    }
    model->page = next;
    if (next != HOLO_PAGE_SPECTRUM) model->spectrum_mode = 0U;
    model->last_command = command;
    model->last_command_ms = now_ms;
    model->revision++;
    return true;
}
