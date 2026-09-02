#include "app_startup.h"

#include "board_laiwfs300.h"

#include "esp_err.h"

esp_err_t app_startup_init(void)
{
    return board_laiwfs300_init();
}
