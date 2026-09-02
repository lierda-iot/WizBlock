#include "audio_ns_demo_logic.h"

#include <assert.h>
#include <stdio.h>

static void test_restart_is_only_allowed_after_cycle_stops(void)
{
    assert(!audio_ns_demo_restart_allowed(AUDIO_NS_DEMO_STATE_PREPARING));
    assert(!audio_ns_demo_restart_allowed(AUDIO_NS_DEMO_STATE_RECORDING));
    assert(!audio_ns_demo_restart_allowed(AUDIO_NS_DEMO_STATE_PLAYING_RAW));
    assert(!audio_ns_demo_restart_allowed(AUDIO_NS_DEMO_STATE_PLAYING_DENOISED));
    assert(audio_ns_demo_restart_allowed(AUDIO_NS_DEMO_STATE_COMPLETE));
    assert(audio_ns_demo_restart_allowed(AUDIO_NS_DEMO_STATE_ERROR));
}

static void test_progress_is_clamped_and_handles_empty_totals(void)
{
    assert(0U == audio_ns_demo_progress_percent(0U, 0U));
    assert(0U == audio_ns_demo_progress_percent(0U, 10U));
    assert(50U == audio_ns_demo_progress_percent(5U, 10U));
    assert(99U == audio_ns_demo_progress_percent(99U, 100U));
    assert(100U == audio_ns_demo_progress_percent(10U, 10U));
    assert(100U == audio_ns_demo_progress_percent(11U, 10U));
}

int main(void)
{
    test_restart_is_only_allowed_after_cycle_stops();
    test_progress_is_clamped_and_handles_empty_totals();
    puts("audio_ns_demo_logic_test: PASS");
    return 0;
}
