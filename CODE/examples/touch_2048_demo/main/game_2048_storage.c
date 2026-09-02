#include "game_2048_storage.h"

#include <stddef.h>

game_2048_storage_load_result_t game_2048_storage_load_best_score(
    const game_2048_storage_backend_t *backend,
    uint32_t *score)
{
    if (NULL == score) {
        return GAME_2048_STORAGE_LOAD_DEFAULTED;
    }
    *score = 0U;
    if ((NULL == backend) || (NULL == backend->read_best_score)) {
        return GAME_2048_STORAGE_LOAD_DEFAULTED;
    }

    uint32_t stored_score = 0U;
    const game_2048_storage_read_result_t result =
        backend->read_best_score(backend->ctx, &stored_score);
    if (GAME_2048_STORAGE_READ_OK != result) {
        return GAME_2048_STORAGE_LOAD_DEFAULTED;
    }
    *score = stored_score;
    return GAME_2048_STORAGE_LOAD_OK;
}

bool game_2048_storage_save_best_score(const game_2048_storage_backend_t *backend,
                                       uint32_t score)
{
    if ((NULL == backend) || (NULL == backend->write_best_score)) {
        return false;
    }
    return backend->write_best_score(backend->ctx, score);
}
