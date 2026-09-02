#include "camera_hal_policy.h"

uint8_t camera_hal_select_sensor_addr(uint8_t configured_addr,
                                      const uint8_t *acked,
                                      size_t acked_len)
{
    if (!acked || configured_addr >= acked_len) {
        return 0;
    }

    return acked[configured_addr] ? configured_addr : 0;
}

bool camera_hal_sensor_id_valid(uint8_t id_high, uint8_t id_low)
{
    return (((uint16_t)id_high << 8) | id_low) == CAMERA_HAL_SP0A39_EXPECTED_ID;
}

bool camera_hal_sensor_output_p0_31_valid(uint8_t value)
{
    return value == CAMERA_HAL_SP0A39_EXPECTED_P0_31;
}
