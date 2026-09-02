#include "game_2048_storage.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

typedef struct {
    game_2048_storage_read_result_t read_result;
    uint32_t stored_score;
    bool write_ok;
    uint32_t written_score;
} storage_fixture_t;

static game_2048_storage_read_result_t read_score(void *ctx, uint32_t *score)
{
    storage_fixture_t *fixture = (storage_fixture_t *)ctx;
    assert(NULL != fixture);
    if (GAME_2048_STORAGE_READ_OK == fixture->read_result) {
        *score = fixture->stored_score;
    }
    return fixture->read_result;
}

static bool write_score(void *ctx, uint32_t score)
{
    storage_fixture_t *fixture = (storage_fixture_t *)ctx;
    assert(NULL != fixture);
    fixture->written_score = score;
    return fixture->write_ok;
}

static void test_load_defaults_on_all_failures(void)
{
    const game_2048_storage_read_result_t failures[] = {
        GAME_2048_STORAGE_READ_NOT_FOUND,
        GAME_2048_STORAGE_READ_INVALID,
        GAME_2048_STORAGE_READ_ERROR,
    };

    for (size_t index = 0U; index < sizeof(failures) / sizeof(failures[0]); ++index) {
        storage_fixture_t fixture = {
            .read_result = failures[index],
            .stored_score = UINT32_MAX,
            .write_ok = true,
            .written_score = 0U,
        };
        const game_2048_storage_backend_t backend = {
            .read_best_score = read_score,
            .write_best_score = write_score,
            .ctx = &fixture,
        };
        uint32_t score = UINT32_MAX;

        assert(GAME_2048_STORAGE_LOAD_DEFAULTED ==
               game_2048_storage_load_best_score(&backend, &score));
        assert(0U == score);
    }
}

static void test_load_and_save(void)
{
    storage_fixture_t fixture = {
        .read_result = GAME_2048_STORAGE_READ_OK,
        .stored_score = 1234U,
        .write_ok = true,
        .written_score = 0U,
    };
    const game_2048_storage_backend_t backend = {
        .read_best_score = read_score,
        .write_best_score = write_score,
        .ctx = &fixture,
    };
    uint32_t score = 0U;

    assert(GAME_2048_STORAGE_LOAD_OK ==
           game_2048_storage_load_best_score(&backend, &score));
    assert(1234U == score);
    assert(game_2048_storage_save_best_score(&backend, UINT32_MAX));
    assert(UINT32_MAX == fixture.written_score);
    fixture.write_ok = false;
    assert(!game_2048_storage_save_best_score(&backend, 9U));
}

int main(void)
{
    test_load_defaults_on_all_failures();
    test_load_and_save();
    puts("game_2048_storage_test: PASS");
    return 0;
}
