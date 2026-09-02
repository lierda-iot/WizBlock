#include "mp3_progress.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_mapping_boundaries(void)
{
    assert(0U == mp3_progress_value_to_ms(-1, 123456U));
    assert(0U == mp3_progress_value_to_ms(0, 123456U));
    assert(61728U == mp3_progress_value_to_ms(500, 123456U));
    assert(123456U == mp3_progress_value_to_ms(1000, 123456U));
    assert(123456U == mp3_progress_value_to_ms(1001, 123456U));
    assert(UINT64_MAX == mp3_progress_value_to_ms(1000, UINT64_MAX));

    assert(0 == mp3_progress_ms_to_value(1U, 0U));
    assert(0 == mp3_progress_ms_to_value(0U, 100U));
    assert(500 == mp3_progress_ms_to_value(50U, 100U));
    assert(1000 == mp3_progress_ms_to_value(100U, 100U));
    assert(1000 == mp3_progress_ms_to_value(101U, 100U));
}

static void test_drag_submits_once_and_preserves_pause(void)
{
    mp3_progress_t progress = {0};
    mp3_seek_request_t request = {0};
    uint64_t preview_ms = 0U;

    mp3_progress_init(&progress);
    mp3_progress_set_snapshot(&progress, 7U, 1000U, 200000U, true);
    assert(mp3_progress_begin_drag(&progress));
    assert(mp3_progress_preview(&progress, 250, &preview_ms));
    assert(50000U == preview_ms);
    assert(mp3_progress_preview(&progress, 750, &preview_ms));
    assert(150000U == preview_ms);
    assert(mp3_progress_release(&progress, 750, &request));
    assert(7U == request.generation);
    assert(150000U == request.target_ms);
    assert(request.keep_paused);
    assert(!mp3_progress_release(&progress, 500, &request));
}

static void test_generation_change_cancels_drag(void)
{
    mp3_progress_t progress = {0};
    mp3_seek_request_t request = {0};

    mp3_progress_init(&progress);
    mp3_progress_set_snapshot(&progress, 1U, 0U, 1000U, false);
    assert(mp3_progress_begin_drag(&progress));
    mp3_progress_set_snapshot(&progress, 2U, 0U, 2000U, false);
    assert(!progress.dragging);
    assert(!mp3_progress_release(&progress, 500, &request));
}

static void test_unknown_duration_and_programmatic_suppression(void)
{
    mp3_progress_t progress = {0};

    mp3_progress_init(&progress);
    mp3_progress_set_snapshot(&progress, 1U, 0U, 0U, false);
    assert(!mp3_progress_begin_drag(&progress));
    mp3_progress_set_snapshot(&progress, 1U, 0U, 1000U, false);
    assert(mp3_progress_begin_drag(&progress));
    assert(mp3_progress_accept_value_event(&progress));
    mp3_progress_set_programmatic_update(&progress, true);
    assert(!mp3_progress_accept_value_event(&progress));
    mp3_progress_set_programmatic_update(&progress, false);
    assert(mp3_progress_accept_value_event(&progress));
    mp3_progress_cancel_drag(&progress);
    assert(!mp3_progress_accept_value_event(&progress));
}

int main(void)
{
    test_mapping_boundaries();
    test_drag_submits_once_and_preserves_pause();
    test_generation_change_cancels_drag();
    test_unknown_duration_and_programmatic_suppression();
    puts("mp3_progress_test: PASS");
    return 0;
}
