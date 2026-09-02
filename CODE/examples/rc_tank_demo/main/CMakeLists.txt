idf_component_register(SRCS "app_main.c"
                             "rc_dvp_staged_override.c"
                             "rc_net.c"
                             "rc_net_send_policy.c"
                             "rc_ctrl_protocol.c"
                             "rc_drive_control.c"
                             "rc_control.c"
                             "rc_video.c"
                             "rc_audio.c"
                             "rc_joystick.c"
                             "rc_tank_screen.c"
                             "rc_capture_pool.c"
                             "rc_capture_pool_target.c"
                             "rc_video_scale.c"
                             "rc_video_yuv_scale.c"
                             "rc_video_synthetic.c"
                             "rc_video_udp_transport.c"
                    INCLUDE_DIRS "."
                    PRIV_INCLUDE_DIRS "${IDF_PATH}/components/esp_driver_cam/dvp/private_include"
                    PRIV_REQUIRES esp_wifi
                                  nvs_flash
                                  esp_netif
                                  esp_event
                                  esp_driver_gpio
                                  esp_adc
                                  driver
                                  esp_driver_i2s
                                  esp_driver_cam
                                  esp_mm
                                  esp_timer
                                  esp_hw_support
                                  camera_hal
                                  display_hal
                                  robot_motion
                                  laiwfs300
                                  es7210_adc
                                  es8311_codec
                                  audio_processor)
