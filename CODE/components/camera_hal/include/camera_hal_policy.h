#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAMERA_HAL_SP0A39_EXPECTED_ID 0x0A39U
#define CAMERA_HAL_SP0A39_EXPECTED_P0_31 0x01U

/* Pure policy seams used by the target HAL and host regression tests. */
uint8_t camera_hal_select_sensor_addr(uint8_t configured_addr,
                                      const uint8_t *acked,
                                      size_t acked_len);
bool camera_hal_sensor_id_valid(uint8_t id_high, uint8_t id_low);
bool camera_hal_sensor_output_p0_31_valid(uint8_t value);
