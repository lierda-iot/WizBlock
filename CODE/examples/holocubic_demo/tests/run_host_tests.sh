#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
demo_dir="$(cd "$script_dir/.." && pwd)"
build_dir="${TMPDIR:-/tmp}/holocubic_demo_tests"

mkdir -p "$build_dir"
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -I "$demo_dir/main" \
  "$script_dir/holocubic_core_test.c" \
  "$demo_dir/main/holocubic_model.c" \
  "$demo_dir/main/holocubic_weather.c" \
  "$demo_dir/main/holocubic_time.c" \
  "$demo_dir/main/holocubic_input.c" \
  "$demo_dir/main/holocubic_wifi.c" \
  "$demo_dir/main/holocubic_ui_state.c" \
  "$demo_dir/main/holocubic_ui_clock.c" \
  "$demo_dir/main/holocubic_wifi_buffer_policy.c" \
  "$demo_dir/main/holocubic_render_policy.c" \
  "$demo_dir/main/holocubic_frame_format.c" \
  "$demo_dir/main/holocubic_periodic.c" \
  "$demo_dir/main/holocubic_visual_ui.c" \
  "$demo_dir/main/holocubic_startup_policy.c" \
  "$demo_dir/main/holocubic_spi_policy.c" \
  "$demo_dir/main/holocubic_network_policy.c" \
  "$demo_dir/main/third_party/jsmn.c" \
  -o "$build_dir/holocubic_core_test"
"$build_dir/holocubic_core_test"

cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -I "$demo_dir/main" \
  "$script_dir/holocubic_spectrum_math_test.c" \
  "$demo_dir/main/audio_spatial_spectrum_math.c" \
  "$demo_dir/main/holocubic_spectrum_raster.c" \
  "$demo_dir/main/holocubic_spectrum_visual_math.c" \
  -lm \
  -o "$build_dir/holocubic_spectrum_math_test"
"$build_dir/holocubic_spectrum_math_test"
