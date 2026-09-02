#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_codec_dev.h"
#include "esp_err.h"
#include "robot_motion.h"

esp_err_t board_laiwfs300_init(void);
bool board_laiwfs300_ioex_available(void);
esp_err_t board_laiwfs300_motor_init(void);
robot_motion_t *board_laiwfs300_motion(void);
void board_laiwfs300_motor_dump_state(void);
esp_err_t board_laiwfs300_display_init(void);
esp_err_t board_laiwfs300_display_init_with_config(uint32_t pixel_clock_hz, int draw_buffer_lines);
esp_err_t board_laiwfs300_display_init_with_pclk(uint32_t pixel_clock_hz);
esp_err_t board_laiwfs300_display_deinit(void);
esp_err_t board_laiwfs300_display_fill_rgb565(uint16_t color);
esp_err_t board_laiwfs300_display_draw_bitmap_rgb565(int x, int y, int w, int h, const uint16_t *data);
esp_err_t board_laiwfs300_camera_init(void);
esp_err_t board_laiwfs300_camera_init_gray_200x200(void);
esp_err_t board_laiwfs300_camera_init_gray_640x480(void);
esp_err_t board_laiwfs300_camera_capture(uint8_t **out_data, size_t *out_len);
void board_laiwfs300_camera_release_frame(uint8_t *data);
esp_err_t board_laiwfs300_touch_init(void);
esp_err_t board_laiwfs300_touch_verify(void);
esp_err_t board_laiwfs300_audio_init(void);
esp_err_t board_laiwfs300_audio_play_tone(uint32_t freq_hz, uint32_t duration_ms);
esp_err_t board_laiwfs300_audio_mic_test(uint32_t duration_ms);
esp_err_t board_laiwfs300_audio_record_play_start(void);
esp_err_t board_laiwfs300_audio_read_raw(int16_t *buf, size_t frames, uint8_t channels);
esp_err_t board_laiwfs300_audio_read_tdm_4ch(int16_t *buf, size_t frames);
esp_err_t board_laiwfs300_audio_open_input_all_channels(void);
esp_codec_dev_handle_t board_laiwfs300_audio_get_output_dev(void);
esp_codec_dev_handle_t board_laiwfs300_audio_get_input_dev(void);
