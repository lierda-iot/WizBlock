#include "companion_core_test_cases.h"

#include "companion_core.h"
#include "companion_controller_model.h"
#include "companion_doa_estimator.h"
#include "companion_logic.h"
#include "companion_merit_tap.h"
#include "companion_network_policy.h"
#include "companion_touch_gesture.h"
#include "companion_turn_control.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

static int s_failure_count;

static void expect_result(const char *const test_name, esp_err_t expected,
                          esp_err_t actual)
{
    if (expected == actual) {
        printf("PASS: %s\n", test_name);
        return;
    }

    printf("FAIL: %s expected=%d actual=%d\n", test_name,
           (int)expected, (int)actual);
    s_failure_count++;
}

static void expect_true(const char *const test_name, bool actual)
{
    if (actual) {
        printf("PASS: %s\n", test_name);
        return;
    }
    printf("FAIL: %s expected=true actual=false\n", test_name);
    s_failure_count++;
}

static void expect_int(const char *const test_name, int expected, int actual)
{
    if (expected == actual) {
        printf("PASS: %s\n", test_name);
        return;
    }
    printf("FAIL: %s expected=%d actual=%d\n", test_name, expected, actual);
    s_failure_count++;
}

/* Test-only adapters keep the cases focused on the closed model interface. */
static esp_err_t test_model_apply(companion_controller_model_t *model,
                                  const companion_controller_input_t *input,
                                  companion_controller_output_t *output)
{
    companion_controller_output_t ignored = {0};
    return companion_controller_model_apply(
        model, input, (NULL != output) ? output : &ignored);
}

static esp_err_t companion_controller_model_finish_startup(
    companion_controller_model_t *model, bool core_ready, uint64_t now_ms)
{
    companion_controller_snapshot_t snapshot = {0};
    companion_controller_model_snapshot(model, &snapshot);
    if (core_ready) {
        const companion_capability_t core_capabilities[] = {
            COMPANION_CAPABILITY_AUDIO,
            COMPANION_CAPABILITY_AGENT,
        };
        for (size_t index = 0U;
             index < sizeof(core_capabilities) / sizeof(core_capabilities[0]);
             ++index) {
            const companion_capability_t capability =
                core_capabilities[index];
            if (0U == snapshot.capability_revisions[capability]) {
                const companion_controller_input_t capability_input = {
                    .type = COMPANION_CONTROLLER_INPUT_CAPABILITY_SNAPSHOT,
                    .now_ms = now_ms,
                    .data.capability = {
                        .capability = capability,
                        .available = true,
                        .error = ESP_OK,
                        .revision = 1U,
                    },
                };
                const esp_err_t capability_result = test_model_apply(
                    model, &capability_input, NULL);
                if (ESP_OK != capability_result) {
                    return capability_result;
                }
            }
        }
        companion_controller_model_snapshot(model, &snapshot);
        if (0U == snapshot.network_revision) {
            const companion_controller_input_t network_input = {
                .type = COMPANION_CONTROLLER_INPUT_NETWORK_SNAPSHOT,
                .now_ms = now_ms,
                .data.network = {
                    .ready = true,
                    .revision = 1U,
                },
            };
            const esp_err_t network_result = test_model_apply(
                model, &network_input, NULL);
            if (ESP_OK != network_result) {
                return network_result;
            }
        }
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_STARTUP_COMPLETE,
        .now_ms = now_ms,
        .data.startup.core_ready = core_ready,
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_reserve_wake(
    companion_controller_model_t *model, uint64_t now_ms,
    uint32_t *generation, uint32_t *wake_seq)
{
    if (NULL == generation || NULL == wake_seq) {
        return ESP_ERR_INVALID_ARG;
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_RESERVE_WAKE,
        .now_ms = now_ms,
    };
    companion_controller_output_t output = {0};
    const esp_err_t result = test_model_apply(model, &input, &output);
    if (ESP_OK == result) {
        *generation = output.generation;
        *wake_seq = output.wake_seq;
    }
    return result;
}

static esp_err_t companion_controller_model_mark_locating_started(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_LOCATING_ACCEPTED,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = now_ms,
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_on_doa(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, bool valid, float relative_deg,
    companion_controller_decision_t *decision)
{
    if (NULL == decision) {
        return ESP_ERR_INVALID_ARG;
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_DOA_COMPLETED,
        .generation = generation,
        .wake_seq = wake_seq,
        .data.doa = {
            .valid = valid,
            .relative_deg = relative_deg,
        },
    };
    companion_controller_output_t output = {0};
    const esp_err_t result = test_model_apply(model, &input, &output);
    *decision = output.decision;
    return result;
}

static esp_err_t companion_controller_model_mark_motion_started(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_MOTION_ACCEPTED,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = now_ms,
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_on_motion_done(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, companion_controller_decision_t *decision)
{
    if (NULL == decision) {
        return ESP_ERR_INVALID_ARG;
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_MOTION_COMPLETED,
        .generation = generation,
        .wake_seq = wake_seq,
    };
    companion_controller_output_t output = {0};
    const esp_err_t result = test_model_apply(model, &input, &output);
    *decision = output.decision;
    return result;
}

static esp_err_t companion_controller_model_mark_agent_accepted(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint32_t request_id, uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_AGENT_ACCEPTED,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = now_ms,
        .data.agent_accepted.request_id = request_id,
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_on_agent_semantic(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint32_t session_epoch, uint32_t request_id,
    companion_agent_semantic_t semantic, esp_err_t result,
    uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_AGENT_SEMANTIC,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = now_ms,
        .data.agent = {
            .session_epoch = session_epoch,
            .request_id = request_id,
            .semantic = semantic,
            .result = result,
        },
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_on_vad_end(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, bool *notify_agent)
{
    if (NULL == notify_agent) {
        return ESP_ERR_INVALID_ARG;
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_VAD_END,
        .generation = generation,
        .wake_seq = wake_seq,
    };
    companion_controller_output_t output = {0};
    const esp_err_t result = test_model_apply(model, &input, &output);
    *notify_agent = output.notify_agent;
    return result;
}

static bool companion_controller_model_should_upload(
    const companion_controller_model_t *model, uint32_t generation)
{
    companion_controller_snapshot_t snapshot = {0};
    companion_controller_model_snapshot(model, &snapshot);
    return snapshot.generation == generation && snapshot.upload_gate_open &&
           COMPANION_PRODUCT_LISTENING == snapshot.product_state;
}

static bool companion_controller_model_poll_deadline(
    companion_controller_model_t *model, uint64_t now_ms,
    companion_controller_deadline_t *expired)
{
    return companion_controller_model_tick(model, now_ms, expired);
}

static void companion_controller_model_set_network(
    companion_controller_model_t *model, bool ready, uint64_t now_ms)
{
    companion_controller_snapshot_t snapshot = {0};
    companion_controller_model_snapshot(model, &snapshot);
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_NETWORK_SNAPSHOT,
        .now_ms = now_ms,
        .data.network = {
            .ready = ready,
            .revision = snapshot.network_revision + 1U,
        },
    };
    (void)test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_enter_error(
    companion_controller_model_t *model,
    companion_controller_error_reason_t reason,
    bool restart_required, uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_ENTER_ERROR,
        .now_ms = now_ms,
        .data.error = {
            .reason = reason,
            .restart_required = restart_required,
        },
    };
    return test_model_apply(model, &input, NULL);
}

static esp_err_t companion_controller_model_recover_error(
    companion_controller_model_t *model,
    companion_controller_error_reason_t reason,
    bool core_ready, uint64_t now_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_RECOVER_ERROR,
        .now_ms = now_ms,
        .data.error = {
            .reason = reason,
            .core_ready = core_ready,
        },
    };
    return test_model_apply(model, &input, NULL);
}

static bool companion_controller_model_toggle_roam(
    companion_controller_model_t *model)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_SW3_CLICK,
    };
    companion_controller_output_t output = {0};
    return ESP_OK == test_model_apply(model, &input, &output) &&
           output.roam_enabled;
}

static void run_behavior_tests(void)
{
    companion_roam_config_t config = {0};
    companion_action_plan_t action_plan = {0};
    companion_turn_plan_t turn_plan = {0};
    int left = 0;
    int right = 0;

    companion_roam_config_default(&config);
    expect_result("default roam configuration is valid", ESP_OK,
                  companion_roam_config_validate(&config));
    expect_int("default roam minimum stop is 3000ms", 3000,
               (int)config.min_stop_ms);
    expect_int("default roam maximum stop is 8000ms", 8000,
               (int)config.max_stop_ms);
    expect_int("default roam initial delay is 3000ms", 3000,
               (int)config.initial_delay_ms);
    expect_result("default roam produces an action", ESP_OK,
                  companion_behavior_plan(&config, 0U, 0U, 0U, &action_plan));
    expect_true("default roam never selects stop as movement",
                COMPANION_ACTION_STOP != action_plan.action);

    companion_roam_config_t invalid = config;
    for (int i = 0; i < COMPANION_ACTION_COUNT; ++i) {
        invalid.actions[i].weight = 0U;
    }
    expect_result("all-zero roam weights are rejected", ESP_ERR_INVALID_ARG,
                  companion_roam_config_validate(&invalid));
    invalid = config;
    invalid.actions[COMPANION_ACTION_FORWARD].min_duration_ms = 900U;
    invalid.actions[COMPANION_ACTION_FORWARD].max_duration_ms = 300U;
    expect_result("reversed duration range is rejected", ESP_ERR_INVALID_ARG,
                  companion_roam_config_validate(&invalid));
    invalid = config;
    invalid.initial_delay_ms = 0U;
    expect_result("zero roam initial delay is rejected", ESP_ERR_INVALID_ARG,
                  companion_roam_config_validate(&invalid));

    expect_result("forward track mapping is valid", ESP_OK,
                  companion_action_tracks(COMPANION_ACTION_FORWARD, &left, &right));
    expect_int("forward left track is 100 percent", 100, left);
    expect_int("forward right track is 100 percent", 100, right);
    expect_result("left turn track mapping is valid", ESP_OK,
                  companion_action_tracks(COMPANION_ACTION_TURN_LEFT, &left, &right));
    expect_int("left turn left track is reverse 100 percent", -100, left);
    expect_int("left turn right track is forward 100 percent", 100, right);

    expect_result("9 degree relative angle is valid", ESP_OK,
                  companion_turn_plan_from_relative(9.0f, &turn_plan));
    expect_int("9 degree relative angle stays in dead zone",
               COMPANION_TURN_NONE, turn_plan.direction);
    expect_result("10 degree relative angle is valid", ESP_OK,
                  companion_turn_plan_from_relative(10.0f, &turn_plan));
    expect_int("10 degree relative angle turns left",
               COMPANION_TURN_LEFT, turn_plan.direction);
    expect_int("10 degree turn uses minimum hard timeout", 1500,
               (int)turn_plan.duration_ms);
    expect_result("52.5 degree relative angle is valid", ESP_OK,
                  companion_turn_plan_from_relative(52.5f, &turn_plan));
    expect_int("52.5 degree turn hard timeout is rounded up to 10ms", 4880,
               (int)turn_plan.duration_ms);
    expect_result("negative 90 degree relative angle is valid", ESP_OK,
                  companion_turn_plan_from_relative(-90.0f, &turn_plan));
    expect_int("negative relative angle turns right",
               COMPANION_TURN_RIGHT, turn_plan.direction);
    expect_true("relative angle is limited to 90 degrees",
                fabsf(turn_plan.relative_deg + 90.0f) < 0.1f);
    expect_int("90 degree turn is limited to 8000ms", 8000,
               (int)turn_plan.duration_ms);
    expect_result("non-finite relative angle is rejected", ESP_ERR_INVALID_ARG,
                  companion_turn_plan_from_relative(NAN, &turn_plan));
}

static void run_merit_tap_tests(void)
{
    companion_merit_tap_config_t config = {0};
    companion_merit_tap_t detector = {0};
    companion_merit_result_t result = {0};
    companion_merit_tap_config_default(&config);
    expect_result("default merit tap configuration initializes", ESP_OK,
                  companion_merit_tap_init(&detector, &config));

    const int16_t stable_values[] = {8180, 8200, 8190, 8210, 8200};
    for (size_t index = 0U;
         index < sizeof(stable_values) / sizeof(stable_values[0]); ++index) {
        const companion_merit_sample_t sample = {
            .accel_z = stable_values[index],
            .timestamp_us = index * 10000U,
        };
        expect_result("stable merit baseline sample is accepted", ESP_OK,
                      companion_merit_tap_push(&detector, &sample, &result));
        expect_true("stable merit baseline does not hit", !result.hit);
    }
    const companion_merit_sample_t impact = {
        .accel_z = 9500,
        .timestamp_us = 50000U,
    };
    expect_result("relative merit impulse starts candidate", ESP_OK,
                  companion_merit_tap_push(&detector, &impact, &result));
    expect_true("relative merit impulse waits for return", !result.hit);
    const companion_merit_sample_t returned = {
        .accel_z = 8220,
        .timestamp_us = 60000U,
    };
    expect_result("relative merit impulse return is accepted", ESP_OK,
                  companion_merit_tap_push(&detector, &returned, &result));
    expect_true("relative merit impulse return emits hit", result.hit);
    expect_int("first relative impulse has no repeat count", 0,
               (int)result.repeat_count);

    companion_merit_tap_reset(&detector);
    const int16_t moving_baseline[] = {7900, 8000, 8100, 8200, 8300};
    for (size_t index = 0U;
         index < sizeof(moving_baseline) / sizeof(moving_baseline[0]);
         ++index) {
        const companion_merit_sample_t sample = {
            .accel_z = moving_baseline[index],
            .timestamp_us = index * 10000U,
        };
        (void)companion_merit_tap_push(&detector, &sample, &result);
    }
    const companion_merit_sample_t moving_impact = {
        .accel_z = 9300,
        .gyro_z = 900,
        .timestamp_us = 50000U,
    };
    expect_result("moving-baseline impulse starts candidate", ESP_OK,
                  companion_merit_tap_push(&detector, &moving_impact,
                                            &result));
    expect_true("moving-baseline impulse waits for return", !result.hit);
    const companion_merit_sample_t moving_return = {
        .accel_z = 8340,
        .gyro_z = 850,
        .timestamp_us = 60000U,
    };
    expect_result("moving-baseline impulse return is accepted", ESP_OK,
                  companion_merit_tap_push(&detector, &moving_return,
                                            &result));
    expect_true("moving-baseline impulse return emits hit", result.hit);

    companion_merit_tap_reset(&detector);
    const int16_t motor_noise[] = {
        7900, 8800, 7600, 9000, 7500, 8700, 7300, 9100, 7700, 8900,
    };
    for (size_t index = 0U;
         index < sizeof(motor_noise) / sizeof(motor_noise[0]); ++index) {
        const companion_merit_sample_t sample = {
            .accel_z = motor_noise[index],
            .gyro_z = 900,
            .timestamp_us = index * 10000U,
        };
        expect_result("sustained motor noise sample is accepted", ESP_OK,
                      companion_merit_tap_push(&detector, &sample, &result));
        expect_true("sustained motor noise does not hit", !result.hit);
    }

    companion_merit_tap_reset(&detector);
    for (size_t index = 0U;
         index < sizeof(stable_values) / sizeof(stable_values[0]); ++index) {
        const companion_merit_sample_t sample = {
            .accel_z = stable_values[index],
            .timestamp_us = index * 10000U,
        };
        (void)companion_merit_tap_push(&detector, &sample, &result);
    }
    expect_result("sustained displacement starts candidate", ESP_OK,
                  companion_merit_tap_push(&detector, &impact, &result));
    const companion_merit_sample_t expired_displacement = {
        .accel_z = 9500,
        .timestamp_us = 210000U,
    };
    expect_result("expired displacement is discarded", ESP_OK,
                  companion_merit_tap_push(&detector,
                                            &expired_displacement, &result));
    expect_true("expired displacement never emits hit", !result.hit);

    const companion_merit_sample_t time_reversal = {
        .accel_z = 8200,
        .timestamp_us = 1U,
    };
    expect_result("timestamp reversal is rejected", ESP_ERR_INVALID_ARG,
                  companion_merit_tap_push(&detector, &time_reversal,
                                            &result));
    config.accel_delta_threshold_raw = 0U;
    expect_result("invalid merit threshold is rejected", ESP_ERR_INVALID_ARG,
                  companion_merit_tap_init(&detector, &config));
}

static void run_expression_tests(void)
{
    companion_expression_model_t model = {0};
    companion_expression_snapshot_t snapshot = {0};

    companion_expression_init(&model);
    expect_result("idle expression state is accepted", ESP_OK,
                  companion_expression_set_product(&model, COMPANION_PRODUCT_IDLE));
    expect_result("idle expression can render", ESP_OK,
                  companion_expression_model_render(&model, 1000U, 1U,
                                                    &snapshot));
    expect_int("idle base expression is smile", COMPANION_EXPRESSION_SMILE,
               snapshot.base);

    expect_result("touch press is accepted", ESP_OK,
                  companion_expression_set_touch(&model, true));
    expect_result("touch expression can render", ESP_OK,
                  companion_expression_model_render(&model, 1100U, 0U,
                                                    &snapshot));
    expect_true("touch overrides base animation with pout",
                COMPANION_EXPRESSION_EFFECT_POUT_IN == snapshot.effect ||
                COMPANION_EXPRESSION_EFFECT_POUT_OUT == snapshot.effect);

    expect_result("speaking product state is accepted during touch", ESP_OK,
                  companion_expression_set_product(&model, COMPANION_PRODUCT_SPEAKING));
    expect_result("touch release is accepted", ESP_OK,
                  companion_expression_set_touch(&model, false));
    expect_result("post-touch expression can render", ESP_OK,
                  companion_expression_model_render(&model, 1400U, 0U,
                                                    &snapshot));
    expect_int("touch release restores latest speaking base",
               COMPANION_EXPRESSION_TALK, snapshot.base);
    expect_true("speaking expression animates mouth",
                COMPANION_EXPRESSION_EFFECT_MOUTH_OPEN == snapshot.effect ||
                COMPANION_EXPRESSION_EFFECT_MOUTH_CLOSED == snapshot.effect);
}

static void run_controller_tests(void)
{
    companion_controller_model_t model = {0};
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;
    companion_controller_decision_t decision = {0};

    companion_controller_model_init(&model, 100U);
    expect_int("controller starts in booting", COMPANION_PRODUCT_BOOTING,
               model.product_state);
    expect_result("booting rejects wake", ESP_ERR_INVALID_STATE,
                  companion_controller_model_reserve_wake(
                      &model, 101U, &generation, &wake_seq));
    expect_result("startup completion enters idle", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 102U));
    expect_result("startup completion is one shot", ESP_ERR_INVALID_STATE,
                  companion_controller_model_finish_startup(
                      &model, true, 103U));

    expect_result("idle controller reserves wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 104U, &generation, &wake_seq));
    expect_int("wake reserve waits for accepted effect",
               COMPANION_PRODUCT_IDLE, model.product_state);
    expect_true("wake reserve is observable", model.wake_reserved);
    expect_true("idle wake requires localization",
                model.wake_requires_localization);
    expect_result("unavailable doa chooses agent effect", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, false, 0.0f, &decision));
    expect_int("unavailable doa requests agent",
               COMPANION_CONTROLLER_DECISION_NOTIFY_AGENT, decision.type);
    expect_int("agent rejection would leave product idle",
               COMPANION_PRODUCT_IDLE, model.product_state);
    expect_result("accepted agent effect enters connecting", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 11U, 105U));
    const uint64_t connecting_deadline = model.state_deadline_ms;
    expect_result("connecting semantic is diagnostic and idempotent", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_CONNECTING, ESP_OK, 106U));
    expect_true("connecting semantic does not extend deadline",
                connecting_deadline == model.state_deadline_ms);
    expect_result("zero agent epoch is rejected", ESP_ERR_INVALID_ARG,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 0U, 11U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 107U));
    expect_result("listening ready opens upload", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 108U));
    const uint64_t listening_deadline = model.state_deadline_ms;
    expect_result("duplicate listening ready is idempotent", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 109U));
    expect_true("duplicate ready does not extend deadline",
                listening_deadline == model.state_deadline_ms);
    expect_true("only current listening generation uploads",
                companion_controller_model_should_upload(&model, generation));
    bool notify_agent = false;
    expect_result("current listening VAD end is accepted", ESP_OK,
                  companion_controller_model_on_vad_end(
                      &model, generation, wake_seq, &notify_agent));
    expect_true("listening VAD end requests one agent stop", notify_agent);
    expect_result("processing closes upload", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_PROCESSING, ESP_OK, 110U));
    expect_true("processing upload is closed",
                !companion_controller_model_should_upload(&model, generation));
    const uint64_t processing_deadline = model.state_deadline_ms;
    expect_result("duplicate processing is idempotent", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_PROCESSING, ESP_OK, 111U));
    expect_true("duplicate processing does not extend deadline",
                processing_deadline == model.state_deadline_ms);
    expect_result("listening ready cannot regress processing",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK,
                      112U));
    expect_result("same request may converge to speaking", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_SPEAKING, ESP_OK, 113U));
    const uint64_t speaking_deadline = model.state_deadline_ms;
    expect_result("duplicate speaking is idempotent", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_SPEAKING, ESP_OK, 114U));
    expect_true("duplicate speaking does not extend deadline",
                speaking_deadline == model.state_deadline_ms);
    expect_result("different request cannot close current session",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 12U,
                      COMPANION_AGENT_SEMANTIC_CLOSED, ESP_OK, 112U));
    expect_result("current closed returns idle", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 21U, 11U,
                      COMPANION_AGENT_SEMANTIC_CLOSED, ESP_OK, 113U));
    expect_true("closed invalidates completed generation",
                generation != model.generation);

    companion_controller_model_init(&model, 200U);
    expect_result("turn test startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 201U));
    expect_result("turn wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 202U, &generation, &wake_seq));
    expect_result("accepted doa effect enters locating", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 203U));
    expect_result("non-dead-zone doa creates turn effect", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, true, -60.0f, &decision));
    expect_int("doa chooses wake turn",
               COMPANION_CONTROLLER_DECISION_START_TURN, decision.type);
    expect_int("turn effect is not published before acceptance",
               COMPANION_PRODUCT_LOCATING, model.product_state);
    expect_result("accepted motion effect enters turning", ESP_OK,
                  companion_controller_model_mark_motion_started(
                      &model, generation, wake_seq, 204U));
    expect_result("stale motion completion is rejected", ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_motion_done(
                      &model, generation + 1U, wake_seq, &decision));
    expect_result("current motion completion continues agent", ESP_OK,
                  companion_controller_model_on_motion_done(
                      &model, generation, wake_seq, &decision));
    expect_result("agent effect after motion is accepted", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 31U, 205U));
    expect_result("first session epoch binds to request", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 41U, 31U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 206U));
    expect_result("later epoch change is stale", ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 42U, 31U,
                      COMPANION_AGENT_SEMANTIC_PROCESSING, ESP_OK, 207U));

    companion_controller_model_init(&model, 300U);
    expect_result("pending VAD startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 301U));
    expect_result("pending VAD wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 302U, &generation, &wake_seq));
    notify_agent = true;
    expect_result("VAD may arrive before agent acceptance", ESP_OK,
                  companion_controller_model_on_vad_end(
                      &model, generation, wake_seq, &notify_agent));
    expect_true("early VAD is pending", model.pending_vad_end && !notify_agent);
    expect_result("pending VAD agent effect accepts", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 51U, 303U));
    expect_result("pending VAD reaches listening ready", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 61U, 51U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 304U));
    expect_result("pending VAD is replayable once ready", ESP_OK,
                  companion_controller_model_on_vad_end(
                      &model, generation, wake_seq, &notify_agent));
    expect_true("pending VAD now notifies agent", notify_agent);

    companion_controller_model_init(&model, 400U);
    companion_controller_deadline_t expired = {0};
    expect_true("boot deadline enters error",
                companion_controller_model_poll_deadline(
                    &model, 30400U, &expired));
    expect_int("boot deadline state is error", COMPANION_PRODUCT_ERROR,
               model.product_state);

    companion_controller_model_init(&model, 500U);
    expect_result("deadline startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 501U));
    expect_result("deadline wake reserves", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 502U, &generation, &wake_seq));
    expect_result("deadline skips doa", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, false, 0.0f, &decision));
    expect_result("deadline agent accepts", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 71U, 503U));
    expect_true("connecting deadline expires",
                companion_controller_model_poll_deadline(
                    &model, 30503U, &expired));
    expect_int("connecting deadline returns idle", COMPANION_PRODUCT_IDLE,
               model.product_state);

    companion_controller_model_init(&model, 600U);
    expect_result("network test startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 601U));
    companion_controller_model_set_network(&model, false, 602U);
    expect_int("idle survives network loss", COMPANION_PRODUCT_IDLE,
               model.product_state);
    expect_true("network loss preserves requested roam", model.roam_enabled);
    expect_true("network loss only closes network gate",
                !model.network_ready && !model.upload_gate_open);
    companion_controller_model_set_network(&model, true, 603U);
    expect_int("network recovery keeps idle state", COMPANION_PRODUCT_IDLE,
               model.product_state);
    expect_true("network recovery preserves requested roam",
                model.roam_enabled);
    expect_result("network test reserves wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 604U, &generation, &wake_seq));
    companion_controller_model_set_network(&model, false, 605U);
    expect_int("network loss cancels reserved wake", COMPANION_PRODUCT_IDLE,
               model.product_state);
    expect_true("network loss clears reservation", !model.wake_reserved);

    expect_result("agent error enters recoverable error", ESP_OK,
                  companion_controller_model_enter_error(
                      &model, COMPANION_CONTROLLER_ERROR_AGENT, false,
                      606U));
    expect_result("matching restored core recovers idle", ESP_OK,
                  companion_controller_model_recover_error(
                      &model, COMPANION_CONTROLLER_ERROR_AGENT,
                      true, 607U));
    expect_result("audio restart-required error enters error", ESP_OK,
                  companion_controller_model_enter_error(
                      &model, COMPANION_CONTROLLER_ERROR_AUDIO,
                      true, 608U));
    expect_result("restart-required error cannot auto recover",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_recover_error(
                      &model, COMPANION_CONTROLLER_ERROR_AUDIO,
                      true, 609U));

    companion_controller_model_init(&model, 608U);
    expect_result("error escalation startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 609U));
    expect_result("error escalation enters recoverable agent error", ESP_OK,
                  companion_controller_model_enter_error(
                      &model, COMPANION_CONTROLLER_ERROR_AGENT,
                      false, 610U));
    expect_result("audio fatal escalates existing agent error", ESP_OK,
                  companion_controller_model_enter_error(
                      &model, COMPANION_CONTROLLER_ERROR_AUDIO,
                      true, 611U));
    expect_int("audio fatal owns escalated error reason",
               COMPANION_CONTROLLER_ERROR_AUDIO, model.error_reason);
    expect_true("audio fatal latches restart-required after escalation",
                model.restart_required);
    expect_result("agent recovery cannot clear escalated audio error",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_recover_error(
                      &model, COMPANION_CONTROLLER_ERROR_AGENT,
                      true, 612U));

    expect_true("roam starts enabled before SW3 click",
                model.roam_enabled);
    expect_true("first SW3 click disables roam",
                !companion_controller_model_toggle_roam(&model));
    expect_true("second SW3 click enables roam",
                companion_controller_model_toggle_roam(&model));
}

static void run_controller_capability_tests(void)
{
    companion_controller_model_t model = {0};
    companion_controller_output_t output = {0};
    companion_controller_snapshot_t snapshot = {0};
    companion_controller_model_init(&model, 0U);

    companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_COUNT,
    };
    expect_result("unknown controller input is rejected",
                  ESP_ERR_INVALID_ARG,
                  test_model_apply(&model, &input, &output));
    companion_controller_model_snapshot(&model, &snapshot);
    expect_int("invalid input leaves controller booting",
               COMPANION_PRODUCT_BOOTING, snapshot.product_state);

    input = (companion_controller_input_t){
        .type = COMPANION_CONTROLLER_INPUT_CAPABILITY_SNAPSHOT,
        .now_ms = 1U,
        .data.capability = {
            .capability = COMPANION_CAPABILITY_AUDIO,
            .available = true,
            .error = ESP_OK,
            .revision = 1U,
        },
    };
    expect_result("audio capability revision is accepted", ESP_OK,
                  test_model_apply(&model, &input, &output));
    input.now_ms = 2U;
    input.data.capability.capability = COMPANION_CAPABILITY_AGENT;
    expect_result("agent capability revision is accepted", ESP_OK,
                  test_model_apply(&model, &input, &output));
    expect_result("capability-ready startup enters idle", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 3U));

    input.now_ms = 4U;
    input.data.capability.available = false;
    input.data.capability.error = ESP_FAIL;
    input.data.capability.revision = 2U;
    expect_result("agent loss enters bounded error", ESP_OK,
                  test_model_apply(&model, &input, &output));
    companion_controller_model_snapshot(&model, &snapshot);
    expect_int("agent loss owns error reason",
               COMPANION_CONTROLLER_ERROR_AGENT, snapshot.error_reason);
    expect_true("agent loss allows controlled recovery",
                !snapshot.restart_required);

    input.data.capability.revision = 1U;
    expect_result("stale capability revision is rejected",
                  ESP_ERR_INVALID_STATE,
                  test_model_apply(&model, &input, &output));
    input.now_ms = 5U;
    input.data.capability.available = true;
    input.data.capability.error = ESP_OK;
    input.data.capability.revision = 3U;
    expect_result("agent recovery revision returns to idle", ESP_OK,
                  test_model_apply(&model, &input, &output));
    companion_controller_model_snapshot(&model, &snapshot);
    expect_int("agent recovery establishes new idle epoch",
               COMPANION_PRODUCT_IDLE, snapshot.product_state);

    input.now_ms = 6U;
    input.data.capability.capability = COMPANION_CAPABILITY_AUDIO;
    input.data.capability.available = false;
    input.data.capability.error = ESP_FAIL;
    input.data.capability.revision = 2U;
    expect_result("audio loss enters restart-required error", ESP_OK,
                  test_model_apply(&model, &input, &output));
    input.now_ms = 7U;
    input.data.capability.available = true;
    input.data.capability.error = ESP_OK;
    input.data.capability.revision = 3U;
    expect_result("audio up revision is recorded", ESP_OK,
                  test_model_apply(&model, &input, &output));
    companion_controller_model_snapshot(&model, &snapshot);
    expect_int("audio fatal does not auto-recover",
               COMPANION_PRODUCT_ERROR, snapshot.product_state);
    expect_true("audio fatal remains restart-required",
                snapshot.restart_required);
}

static void run_controller_interrupt_tests(void)
{
    companion_controller_model_t model = {0};
    companion_controller_decision_t decision = {0};
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;

    companion_controller_model_init(&model, 700U);
    expect_result("interrupt startup completes", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 701U));
    expect_result("interrupt setup reserves wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 702U, &generation, &wake_seq));
    expect_result("interrupt setup skips doa", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, false, 0.0f, &decision));
    expect_result("interrupt setup accepts agent", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 81U, 703U));
    expect_result("interrupt setup enters listening", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 91U, 81U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 704U));
    expect_result("interrupt setup enters processing", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 91U, 81U,
                      COMPANION_AGENT_SEMANTIC_PROCESSING, ESP_OK, 705U));

    const uint32_t old_generation = generation;
    const uint32_t old_wake_seq = wake_seq;
    expect_result("processing accepts a new wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 706U, &generation, &wake_seq));
    expect_true("processing re-wake requires localization",
                model.wake_requires_localization);
    expect_result("processing re-wake enters locating", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 707U));
    expect_result("processing re-wake DOA requests a turn", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, true, 30.0f, &decision));
    expect_int("processing re-wake waits for localization turn",
               COMPANION_CONTROLLER_DECISION_START_TURN, decision.type);
    expect_result("processing re-wake turn starts", ESP_OK,
                  companion_controller_model_mark_motion_started(
                      &model, generation, wake_seq, 708U));
    expect_result("processing re-wake turn completes", ESP_OK,
                  companion_controller_model_on_motion_done(
                      &model, generation, wake_seq, &decision));
    expect_int("processing re-wake resumes Agent after turn",
               COMPANION_CONTROLLER_DECISION_NOTIFY_AGENT, decision.type);
    expect_result("processing interrupt accepts new request", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 82U, 709U));
    expect_result("old closed cannot finish new request",
                  ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_agent_semantic(
                      &model, old_generation, old_wake_seq, 91U, 81U,
                      COMPANION_AGENT_SEMANTIC_CLOSED, ESP_OK, 710U));
    expect_int("stale close keeps new request connecting",
               COMPANION_PRODUCT_CONNECTING, model.product_state);
    expect_result("same-state relisten emits ready for new request", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 91U, 82U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 711U));
    expect_result("new request may move directly to speaking", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 91U, 82U,
                      COMPANION_AGENT_SEMANTIC_SPEAKING, ESP_OK, 712U));

    const uint32_t speaking_generation = generation;
    const uint32_t speaking_wake_seq = wake_seq;
    expect_result("speaking accepts another wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 713U, &generation, &wake_seq));
    expect_true("speaking re-wake requires localization",
                model.wake_requires_localization);
    expect_result("speaking interrupt uses unavailable-doa fallback", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, false, 0.0f, &decision));
    expect_result("speaking interrupt accepts new request", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 83U, 714U));
    expect_result("old speaking stop is rejected", ESP_ERR_INVALID_STATE,
                  companion_controller_model_on_agent_semantic(
                      &model, speaking_generation, speaking_wake_seq,
                      91U, 82U, COMPANION_AGENT_SEMANTIC_CLOSED,
                      ESP_OK, 715U));
    expect_result("new interrupted request reaches listening", ESP_OK,
                  companion_controller_model_on_agent_semantic(
                      &model, generation, wake_seq, 91U, 83U,
                      COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK, 716U));

    expect_result("listening re-wake accepts a new request", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 717U, &generation, &wake_seq));
    expect_true("listening re-wake requires localization",
                model.wake_requires_localization);
    expect_result("listening re-wake request is accepted", ESP_OK,
                  companion_controller_model_mark_agent_accepted(
                      &model, generation, wake_seq, 84U, 718U));
    expect_result("connecting accepts a newer wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 719U, &generation, &wake_seq));
    expect_true("connecting re-wake requires localization",
                model.wake_requires_localization);
}

static void run_controller_deadline_matrix_tests(void)
{
    companion_controller_model_t model = {0};
    companion_controller_decision_t decision = {0};
    companion_controller_deadline_t expired = {0};
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;

    companion_controller_model_init(&model, 0U);
    expect_result("locating deadline setup startup", ESP_OK,
                  companion_controller_model_finish_startup(&model, true, 1U));
    expect_result("locating deadline setup wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 2U, &generation, &wake_seq));
    expect_result("locating deadline setup accepted", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 3U));
    expect_true("locating deadline is delegated to controller fallback",
                companion_controller_model_poll_deadline(
                    &model, 2003U, &expired));
    expect_int("locating deadline does not cancel AI transaction",
               COMPANION_PRODUCT_LOCATING, model.product_state);

    companion_controller_model_init(&model, 10U);
    expect_result("turning deadline setup startup", ESP_OK,
                  companion_controller_model_finish_startup(&model, true, 11U));
    expect_result("turning deadline setup wake", ESP_OK,
                  companion_controller_model_reserve_wake(
                      &model, 12U, &generation, &wake_seq));
    expect_result("turning deadline setup locating", ESP_OK,
                  companion_controller_model_mark_locating_started(
                      &model, generation, wake_seq, 13U));
    expect_result("turning deadline setup plan", ESP_OK,
                  companion_controller_model_on_doa(
                      &model, generation, wake_seq, true, 30.0f, &decision));
    expect_result("turning deadline setup accepted", ESP_OK,
                  companion_controller_model_mark_motion_started(
                      &model, generation, wake_seq, 14U));
    expect_true("turning remains active before 14 second deadline",
                !companion_controller_model_poll_deadline(
                    &model, 14013U, &expired));
    expect_true("turning deadline is delegated to controller fallback",
                companion_controller_model_poll_deadline(
                    &model, 14014U, &expired));
    expect_int("turning deadline does not cancel AI transaction",
               COMPANION_PRODUCT_TURNING, model.product_state);

    const companion_product_state_t deadline_states[] = {
        COMPANION_PRODUCT_LISTENING,
        COMPANION_PRODUCT_PROCESSING,
        COMPANION_PRODUCT_SPEAKING,
    };
    const uint64_t deadline_durations[] = {30000U, 17000U, 60000U};
    for (size_t index = 0U;
         index < sizeof(deadline_states) / sizeof(deadline_states[0]);
         ++index) {
        const uint64_t base = 100U + (uint64_t)index * 100U;
        companion_controller_model_init(&model, base);
        expect_result("agent deadline setup startup", ESP_OK,
                      companion_controller_model_finish_startup(
                          &model, true, base + 1U));
        expect_result("agent deadline setup wake", ESP_OK,
                      companion_controller_model_reserve_wake(
                          &model, base + 2U, &generation, &wake_seq));
        expect_result("agent deadline setup skip doa", ESP_OK,
                      companion_controller_model_on_doa(
                          &model, generation, wake_seq, false, 0.0f,
                          &decision));
        expect_result("agent deadline setup accepted", ESP_OK,
                      companion_controller_model_mark_agent_accepted(
                          &model, generation, wake_seq, 100U + (uint32_t)index,
                          base + 3U));
        expect_result("agent deadline setup listening", ESP_OK,
                      companion_controller_model_on_agent_semantic(
                          &model, generation, wake_seq, 200U + (uint32_t)index,
                          100U + (uint32_t)index,
                          COMPANION_AGENT_SEMANTIC_LISTENING_READY, ESP_OK,
                          base + 4U));
        if (COMPANION_PRODUCT_PROCESSING == deadline_states[index] ||
            COMPANION_PRODUCT_SPEAKING == deadline_states[index]) {
            expect_result("agent deadline setup processing", ESP_OK,
                          companion_controller_model_on_agent_semantic(
                              &model, generation, wake_seq,
                              200U + (uint32_t)index,
                              100U + (uint32_t)index,
                              COMPANION_AGENT_SEMANTIC_PROCESSING, ESP_OK,
                              base + 5U));
        }
        uint64_t entered_ms = (COMPANION_PRODUCT_LISTENING ==
                               deadline_states[index]) ? base + 4U :
                              base + 5U;
        if (COMPANION_PRODUCT_SPEAKING == deadline_states[index]) {
            expect_result("agent deadline setup speaking", ESP_OK,
                          companion_controller_model_on_agent_semantic(
                              &model, generation, wake_seq,
                              200U + (uint32_t)index,
                              100U + (uint32_t)index,
                              COMPANION_AGENT_SEMANTIC_SPEAKING, ESP_OK,
                              base + 6U));
            entered_ms = base + 6U;
        }
        expect_true("agent activity deadline expires",
                    companion_controller_model_poll_deadline(
                        &model, entered_ms + deadline_durations[index],
                        &expired));
        expect_int("agent activity deadline returns idle",
                   COMPANION_PRODUCT_IDLE, model.product_state);
    }

    companion_controller_model_init(&model, 500U);
    expect_result("error network test startup", ESP_OK,
                  companion_controller_model_finish_startup(
                      &model, true, 501U));
    expect_result("error network test enters restart-required error", ESP_OK,
                  companion_controller_model_enter_error(
                      &model, COMPANION_CONTROLLER_ERROR_AUDIO, true, 502U));
    companion_controller_model_set_network(&model, false, 503U);
    expect_int("network loss does not overwrite error state",
               COMPANION_PRODUCT_ERROR, model.product_state);
    expect_int("network loss keeps original error reason",
               COMPANION_CONTROLLER_ERROR_AUDIO, model.error_reason);
    expect_true("network loss keeps restart-required latch",
                model.restart_required);
}

static companion_touch_gesture_config_t touch_test_config(void)
{
    return (companion_touch_gesture_config_t){
        .display_width = 320U,
        .display_height = 240U,
        .press_debounce_ms = 30U,
        .touch_decision_ms = 150U,
        .release_debounce_ms = 120U,
        .tap_feedback_ms = 220U,
        .swipe_intent_horizontal_px = 12U,
        .swipe_min_horizontal_px = 30U,
        .swipe_max_vertical_px = 32U,
        .swipe_max_duration_ms = 700U,
    };
}

static void run_touch_gesture_tests(void)
{
    const companion_touch_gesture_config_t config = touch_test_config();
    companion_touch_gesture_t gesture = {0};
    companion_touch_gesture_result_t result = {0};

    expect_result("touch gesture config is valid", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    expect_result("12px intent starts contact", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 100U, 100U, 0U, &result));
    expect_result("12px intent is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 112U, 100U, 40U, &result));
    expect_int("12px intent does not switch pack",
               COMPANION_GESTURE_PACK_NONE, result.pack_step);
    expect_result("29px swipe sample is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 129U, 100U, 100U, &result));
    expect_int("29px does not switch pack",
               COMPANION_GESTURE_PACK_NONE, result.pack_step);

    expect_result("30px gesture reset", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 0U, &result);
    (void)companion_touch_gesture_update(
        &gesture, true, 112U, 100U, 40U, &result);
    expect_result("30px swipe is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 130U, 100U, 100U, &result));
    expect_int("30px switches exactly one previous pack",
               COMPANION_GESTURE_PACK_PREVIOUS, result.pack_step);
    expect_result("continued swipe is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 170U, 100U, 110U, &result));
    expect_int("one contact cannot switch a second pack",
               COMPANION_GESTURE_PACK_NONE, result.pack_step);

    expect_result("150ms touch reset", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 0U, &result);
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 149U, &result);
    expect_int("149ms does not publish touch",
               COMPANION_TOUCH_TRANSITION_NONE, result.touch_transition);
    expect_result("150ms touch is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 100U, 100U, 150U, &result));
    expect_int("150ms publishes touch press",
               COMPANION_TOUCH_TRANSITION_PRESSED,
               result.touch_transition);
    expect_result("touch-active movement is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 150U, 100U, 160U, &result));
    expect_int("touch-active movement cannot become swipe",
               COMPANION_GESTURE_PACK_NONE, result.pack_step);

    expect_result("700ms swipe reset", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 0U, &result);
    (void)companion_touch_gesture_update(
        &gesture, true, 112U, 100U, 40U, &result);
    expect_result("700ms boundary swipe is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 130U, 100U, 700U, &result));
    expect_int("700ms boundary can switch pack",
               COMPANION_GESTURE_PACK_PREVIOUS, result.pack_step);

    expect_result("701ms swipe reset", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 0U, &result);
    (void)companion_touch_gesture_update(
        &gesture, true, 112U, 100U, 40U, &result);
    expect_result("701ms late swipe is accepted as input", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 130U, 100U, 701U, &result));
    expect_int("701ms late swipe cannot switch pack",
               COMPANION_GESTURE_PACK_NONE, result.pack_step);

    expect_result("quick tap reset", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 0U, &result);
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 30U, &result);
    expect_result("quick tap release is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, false, 100U, 100U, 40U, &result));
    expect_int("quick tap publishes synthetic press",
               COMPANION_TOUCH_TRANSITION_PRESSED,
               result.touch_transition);
    expect_true("quick tap press is marked synthetic",
                result.synthetic_feedback);
    expect_result("quick tap feedback expiry is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, false, 100U, 100U, 260U, &result));
    expect_int("quick tap publishes synthetic release",
               COMPANION_TOUCH_TRANSITION_RELEASED,
               result.touch_transition);

    expect_result("quick tap recontact reset", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 0U, &result);
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 30U, &result);
    (void)companion_touch_gesture_update(
        &gesture, false, 100U, 100U, 40U, &result);
    expect_result("new contact during quick tap is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 140U, 100U, 100U, &result));
    expect_int("new contact first releases synthetic feedback",
               COMPANION_TOUCH_TRANSITION_RELEASED,
               result.touch_transition);
    expect_int("new contact immediately enters debounce",
               COMPANION_GESTURE_CONTACT_DEBOUNCE,
               gesture.contact_state);
    expect_result("recontact reaches touch decision", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 140U, 100U, 250U, &result));
    expect_int("recontact publishes one normal press",
               COMPANION_TOUCH_TRANSITION_PRESSED,
               result.touch_transition);
    expect_true("recontact press is not synthetic",
                !result.synthetic_feedback);

    expect_result("diagonal touch reset", ESP_OK,
                  companion_touch_gesture_init(&gesture, &config));
    (void)companion_touch_gesture_update(
        &gesture, true, 100U, 100U, 0U, &result);
    expect_result("diagonal movement is accepted", ESP_OK,
                  companion_touch_gesture_update(
                      &gesture, true, 130U, 131U, 150U, &result));
    expect_int("diagonal movement does not switch pack",
               COMPANION_GESTURE_PACK_NONE, result.pack_step);
    expect_int("diagonal movement resolves as touch",
               COMPANION_TOUCH_TRANSITION_PRESSED,
               result.touch_transition);
}

static void run_doa_estimator_tests(void)
{
    companion_doa_estimate_t estimate = {0};
    const float raw_40_left[] = {50.0f, 50.0f, 50.0f, 50.0f};
    expect_true("DOA raw 40 degree left consensus is valid",
                companion_doa_estimate(raw_40_left, 4U, &estimate));
    expect_true("DOA exposes raw relative angle for diagnostics",
                fabsf(estimate.raw_relative_deg - 40.0f) < 0.1f);
    expect_true("DOA maps raw 40 degrees to actual 90 degrees",
                fabsf(estimate.relative_deg - 90.0f) < 0.1f);

    const float raw_20_left[] = {70.0f, 70.0f, 70.0f, 70.0f};
    expect_true("DOA raw 20 degree left consensus is valid",
                companion_doa_estimate(raw_20_left, 4U, &estimate));
    expect_true("DOA maps raw 20 degrees to actual 45 degrees",
                fabsf(estimate.relative_deg - 45.0f) < 0.1f);
    companion_turn_plan_t turn_plan = {0};
    expect_result("actual DOA angle enters the public turn planner", ESP_OK,
                  companion_turn_plan_from_relative(estimate.relative_deg,
                                                    &turn_plan));
    expect_true("turn planner receives actual 45 degree target",
                fabsf(turn_plan.relative_deg - 45.0f) < 0.1f);

    const float raw_20_right[] = {110.0f, 110.0f, 110.0f, 110.0f};
    expect_true("DOA raw 20 degree right consensus is valid",
                companion_doa_estimate(raw_20_right, 4U, &estimate));
    expect_int("DOA right consensus direction", COMPANION_DOA_DIRECTION_RIGHT,
               estimate.direction);
    expect_true("DOA maps right direction to negative actual angle",
                fabsf(estimate.relative_deg + 45.0f) < 0.1f);

    const float raw_60_right[] = {150.0f, 150.0f, 150.0f, 150.0f};
    expect_true("DOA high right consensus is valid",
                companion_doa_estimate(raw_60_right, 4U, &estimate));
    expect_true("DOA actual angle is clamped at negative 90 degrees",
                fabsf(estimate.relative_deg + 90.0f) < 0.1f);

    const float actual_9_left[] = {86.0f, 86.0f, 86.0f, 86.0f};
    expect_true("DOA actual 9 degree sample is valid",
                companion_doa_estimate(actual_9_left, 4U, &estimate));
    expect_result("actual 9 degree target enters planner", ESP_OK,
                  companion_turn_plan_from_relative(estimate.relative_deg,
                                                    &turn_plan));
    expect_int("actual 9 degree target stays in dead zone",
               COMPANION_TURN_NONE, turn_plan.direction);

    const float actual_10_left[] = {85.55556f, 85.55556f,
                                    85.55556f, 85.55556f};
    expect_true("DOA actual 10 degree sample is valid",
                companion_doa_estimate(actual_10_left, 4U, &estimate));
    expect_result("actual 10 degree target enters planner", ESP_OK,
                  companion_turn_plan_from_relative(estimate.relative_deg,
                                                    &turn_plan));
    expect_int("actual 10 degree target starts a turn",
               COMPANION_TURN_LEFT, turn_plan.direction);

    const float center[] = {88.0f, 90.0f, 92.0f, 89.0f, 91.0f, 90.0f};
    expect_true("DOA center consensus is valid",
                companion_doa_estimate(center, 6U, &estimate));
    expect_true("DOA center does not turn", fabsf(estimate.relative_deg) < 0.1f);

    const float split[] = {30.0f, 35.0f, 40.0f, 130.0f, 135.0f, 140.0f};
    expect_true("DOA split directions are rejected",
                !companion_doa_estimate(split, 6U, &estimate));
    expect_true("DOA insufficient samples are rejected",
                !companion_doa_estimate(center, 3U, &estimate));

    const float scattered[] = {5.0f, 10.0f, 20.0f, 45.0f, 65.0f, 70.0f};
    expect_true("DOA dispersed direction is rejected",
                !companion_doa_estimate(scattered, 6U, &estimate));

    const float invalid[] = {0.0f, NAN, -2.0f, 181.0f, 40.0f, 42.0f,
                             44.0f, 46.0f, 48.0f, 50.0f};
    expect_true("DOA invalid samples are ignored",
                companion_doa_estimate(invalid, 10U, &estimate));
    expect_int("DOA invalid samples do not add votes", 6,
               estimate.sample_count);
}

static void run_imu_turn_control_tests(void)
{
    companion_turn_control_config_t config = {0};
    companion_turn_control_t control = {0};
    bool complete = false;

    companion_turn_control_config_default(&config);
    expect_true("IMU turn default stop lead is zero",
                fabsf(config.stop_lead_deg) < 0.001f);
    expect_int("IMU turn default hard timeout is 8000ms", 8000,
               (int)config.hard_timeout_ms);
    config.hard_timeout_ms = 1000U;
    const companion_imu_calibration_t z_axis_calibration = {
        .gyro_bias_raw = {0.0f, 0.0f, 100.0f},
        .gravity_unit = {0.0f, 0.0f, 1.0f},
    };
    expect_result("IMU turn accepts target and six-axis calibration", ESP_OK,
                  companion_turn_control_start(&control, &config, 30.0f,
                                               &z_axis_calibration));
    esp_err_t result = ESP_OK;
    for (int sample = 0; sample < 60 && !complete && ESP_OK == result;
         ++sample) {
        const uint32_t dt_us = (0 == (sample & 1)) ? 8000U : 12000U;
        const companion_imu_sample_t imu_sample = {
            .gyro_z_raw = 1740,
            .accel_z_raw = 8192,
            .accel_magnitude_raw = 8192.0f,
        };
        result = companion_turn_control_update_sample_us(
            &control, &config, &imu_sample, dt_us, &complete);
    }
    expect_result("IMU turn reaches closed-loop stop", ESP_OK, result);
    expect_true("IMU turn completes without early stop", complete);
    expect_true("IMU turn reaches full target angle",
                control.turned_deg >= 30.0f);
    expect_true("IMU turn reports zero actual remaining angle",
                companion_turn_control_remaining_deg(&control) < 0.001f);

    const companion_imu_sample_t stable[] = {
        {.gyro_x_raw = 10, .gyro_y_raw = -5, .gyro_z_raw = 100,
         .accel_z_raw = 8190, .accel_magnitude_raw = 8190.0f},
        {.gyro_x_raw = 15, .gyro_y_raw = 2, .gyro_z_raw = 105,
         .accel_z_raw = 8210, .accel_magnitude_raw = 8210.0f},
        {.gyro_x_raw = -8, .gyro_y_raw = 4, .gyro_z_raw = 98,
         .accel_z_raw = 8170, .accel_magnitude_raw = 8170.0f},
        {.gyro_x_raw = 7, .gyro_y_raw = -3, .gyro_z_raw = 103,
         .accel_z_raw = 8200, .accel_magnitude_raw = 8200.0f},
    };
    expect_true("stationary six-axis window is stable",
                companion_imu_samples_stable(stable, 4U, 82, 800.0f));
    companion_imu_sample_t gyro_moving[4] = {
        stable[0], stable[1], stable[2], stable[3]
    };
    gyro_moving[3].gyro_z_raw = 400;
    expect_true("gyro range over boundary is not stable",
                !companion_imu_samples_stable(gyro_moving, 4U, 82, 800.0f));
    companion_imu_sample_t accel_boundary[2] = {
        {.accel_z_raw = 8000, .accel_magnitude_raw = 8000.0f},
        {.accel_z_raw = 8800, .accel_magnitude_raw = 8800.0f},
    };
    expect_true("accel range at boundary is stable",
                companion_imu_samples_stable(accel_boundary, 2U, 82, 800.0f));
    accel_boundary[1].accel_magnitude_raw = 8800.1f;
    expect_true("accel range over boundary is not stable",
                !companion_imu_samples_stable(accel_boundary, 2U, 82, 800.0f));

    const companion_imu_sample_t calibration_samples[] = {
        {-2000, 195, 295, 0, 0, 8192, 8192.0f},
        {95, 198, 298, 0, 0, 8192, 8192.0f},
        {98, 199, 299, 0, 0, 8192, 8192.0f},
        {99, 200, 300, 0, 0, 8192, 8192.0f},
        {100, 200, 300, 0, 0, 8192, 8192.0f},
        {100, 200, 300, 0, 0, 8192, 8192.0f},
        {101, 201, 301, 0, 0, 8192, 8192.0f},
        {102, 202, 302, 0, 0, 8192, 8192.0f},
        {105, 205, 305, 0, 0, 8192, 8192.0f},
        {2500, 2200, 2300, 0, 0, 8192, 8192.0f},
    };
    companion_imu_calibration_t calibration = {0};
    expect_result("six-axis calibration rejects endpoint outliers", ESP_OK,
                  companion_imu_estimate_calibration(
                      calibration_samples, 10U, 1U, 20, &calibration));
    expect_true("three-axis gyro bias remains near stationary center",
                fabsf(calibration.gyro_bias_raw[0] - 100.0f) < 0.1f &&
                fabsf(calibration.gyro_bias_raw[1] - 200.625f) < 0.1f &&
                fabsf(calibration.gyro_bias_raw[2] - 300.625f) < 0.1f);
    expect_true("stationary acceleration defines the gravity unit axis",
                fabsf(calibration.gravity_unit[0]) < 0.001f &&
                fabsf(calibration.gravity_unit[1]) < 0.001f &&
                fabsf(calibration.gravity_unit[2] - 1.0f) < 0.001f);

    const float inv_sqrt_two = 0.70710678f;
    const companion_imu_calibration_t tilted_calibration = {
        .gyro_bias_raw = {100.0f, 200.0f, 300.0f},
        .gravity_unit = {inv_sqrt_two, 0.0f, inv_sqrt_two},
    };
    config.hard_timeout_ms = 1000U;
    expect_result("tilted IMU turn accepts gravity-axis calibration", ESP_OK,
                  companion_turn_control_start(&control, &config, 20.0f,
                                               &tilted_calibration));
    complete = false;
    result = ESP_OK;
    for (int sample = 0; sample < 40 && !complete && ESP_OK == result;
         ++sample) {
        const companion_imu_sample_t tilted_sample = {
            .gyro_x_raw = 1260,
            .gyro_y_raw = 200,
            .gyro_z_raw = 1460,
            .accel_x_raw = 5793,
            .accel_z_raw = 5793,
            .accel_magnitude_raw = 8192.0f,
        };
        result = companion_turn_control_update_sample_us(
            &control, &config, &tilted_sample, 10000U, &complete);
    }
    expect_result("tilted IMU reaches gravity-axis target", ESP_OK, result);
    expect_true("tilted IMU projection completes the turn", complete);
    expect_true("tilted IMU projected yaw is near 100 degrees per second",
                fabsf(fabsf(control.projected_rate_dps) - 100.0f) < 0.2f);

    expect_result("IMU stall setup succeeds", ESP_OK,
                  companion_turn_control_start(&control, &config, 45.0f,
                                               &z_axis_calibration));
    result = ESP_OK;
    for (int sample = 0; sample < 40 && ESP_OK == result; ++sample) {
        const companion_imu_sample_t stationary_sample = {
            .gyro_z_raw = 100,
            .accel_z_raw = 8192,
            .accel_magnitude_raw = 8192.0f,
        };
        result = companion_turn_control_update_sample_us(
            &control, &config, &stationary_sample, 10000U, &complete);
    }
    expect_result("IMU turn stops on stalled chassis", ESP_ERR_TIMEOUT,
                  result);
}

static void run_network_policy_tests(void)
{
    companion_network_policy_state_t state = {0};
    companion_network_policy_init(&state);

    const companion_network_policy_input_t input = {
        .link_up = true,
        .ipv4_ready = true,
        .internet_reachable = false,
        .disconnect_grace_active = false,
        .ipv4_wait_timed_out = true,
    };
    expect_result("IPv4 without internet is accepted by network policy",
                  ESP_OK, companion_network_policy_apply(&state, &input));
    expect_int("IPv4 without internet waits for reachability",
               COMPANION_NETWORK_PHASE_WAIT_INTERNET, state.phase);
    expect_true("IPv4 state remains observable", state.link_up &&
                state.ipv4_ready && !state.internet_reachable);
    expect_true("IPv4 without internet keeps AI gate closed", !state.ready);
    expect_result("IPv4 timeout is ignored after address acquisition",
                  ESP_OK, state.error);

    const companion_network_policy_input_t ready_input = {
        .link_up = true,
        .ipv4_ready = true,
        .internet_reachable = true,
        .disconnect_grace_active = false,
        .ipv4_wait_timed_out = false,
    };
    expect_result("Internet reachable is accepted by network policy",
                  ESP_OK,
                  companion_network_policy_apply(&state, &ready_input));
    expect_int("Internet reachable opens READY_4G",
               COMPANION_NETWORK_PHASE_READY_4G, state.phase);
    expect_true("READY_4G exposes all network layers",
                state.link_up && state.ipv4_ready &&
                    state.internet_reachable);
    expect_true("READY_4G opens AI gate", state.ready);

    const companion_network_policy_input_t internet_loss_input = {
        .link_up = true,
        .ipv4_ready = true,
        .internet_reachable = false,
        .disconnect_grace_active = false,
        .ipv4_wait_timed_out = false,
    };
    expect_result("Internet loss is accepted by network policy", ESP_OK,
                  companion_network_policy_apply(&state,
                                                 &internet_loss_input));
    expect_int("Internet loss returns to WAIT_INTERNET",
               COMPANION_NETWORK_PHASE_WAIT_INTERNET, state.phase);
    expect_true("Internet loss keeps IPv4 observable",
                state.link_up && state.ipv4_ready &&
                    !state.internet_reachable);
    expect_true("Internet loss closes AI gate", !state.ready);

    const companion_network_policy_input_t disconnect_input = {
        .link_up = false,
        .ipv4_ready = false,
        .internet_reachable = false,
        .disconnect_grace_active = true,
        .ipv4_wait_timed_out = false,
    };
    expect_result("Disconnect grace is accepted by network policy", ESP_OK,
                  companion_network_policy_apply(&state,
                                                 &disconnect_input));
    expect_int("Disconnect enters bounded grace",
               COMPANION_NETWORK_PHASE_DISCONNECT_GRACE, state.phase);
    expect_true("Disconnect clears all network layers",
                !state.link_up && !state.ipv4_ready &&
                    !state.internet_reachable);
    expect_true("Disconnect grace keeps AI gate closed", !state.ready);

    const companion_network_policy_input_t ipv4_timeout_input = {
        .link_up = false,
        .ipv4_ready = false,
        .internet_reachable = false,
        .disconnect_grace_active = false,
        .ipv4_wait_timed_out = true,
    };
    expect_result("IPv4 wait timeout is accepted by network policy", ESP_OK,
                  companion_network_policy_apply(&state,
                                                 &ipv4_timeout_input));
    expect_int("IPv4 wait timeout remains passive WAIT_4G",
               COMPANION_NETWORK_PHASE_WAIT_4G, state.phase);
    expect_true("IPv4 wait timeout never enters destructive recovery",
                COMPANION_NETWORK_PHASE_RECOVERING != state.phase &&
                    COMPANION_NETWORK_PHASE_BACKOFF != state.phase);
    expect_true("IPv4 wait timeout clears all network layers",
                !state.link_up && !state.ipv4_ready &&
                    !state.internet_reachable && !state.ready);
    expect_result("IPv4 wait timeout stays observable", ESP_ERR_TIMEOUT,
                  state.error);
}

int companion_core_run_tests(void)
{
    s_failure_count = 0;
    run_behavior_tests();
    run_merit_tap_tests();
    run_expression_tests();
    run_controller_tests();
    run_controller_capability_tests();
    run_controller_interrupt_tests();
    run_controller_deadline_matrix_tests();
    run_touch_gesture_tests();
    run_doa_estimator_tests();
    run_imu_turn_control_tests();
    run_network_policy_tests();

    printf("companion_core_test: %s (%d failures)\n",
           (0 == s_failure_count) ? "PASS" : "FAIL", s_failure_count);
    return s_failure_count;
}
