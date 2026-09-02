#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GAME_2048_STORAGE_LOAD_OK = 0,
    GAME_2048_STORAGE_LOAD_DEFAULTED,
} game_2048_storage_load_result_t;

typedef enum {
    GAME_2048_STORAGE_READ_OK = 0,
    GAME_2048_STORAGE_READ_NOT_FOUND,
    GAME_2048_STORAGE_READ_INVALID,
    GAME_2048_STORAGE_READ_ERROR,
} game_2048_storage_read_result_t;

typedef game_2048_storage_read_result_t (*game_2048_storage_read_fn_t)(void *ctx,
                                                                        uint32_t *score);
typedef bool (*game_2048_storage_write_fn_t)(void *ctx, uint32_t score);

typedef struct {
    game_2048_storage_read_fn_t read_best_score;
    game_2048_storage_write_fn_t write_best_score;
    void *ctx;
} game_2048_storage_backend_t;

game_2048_storage_load_result_t game_2048_storage_load_best_score(
    const game_2048_storage_backend_t *backend,
    uint32_t *score);
bool game_2048_storage_save_best_score(const game_2048_storage_backend_t *backend,
                                       uint32_t score);
