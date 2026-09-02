#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAIN_DIR="${SCRIPT_DIR}/../main"
BUILD_DIR="${SCRIPT_DIR}/.build"
CC="${CC:-cc}"
CFLAGS=(-std=c11 -Wall -Wextra -Werror -pedantic -I"${MAIN_DIR}")

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

"${CC}" "${CFLAGS[@]}" \
    "${SCRIPT_DIR}/game_2048_core_test.c" \
    "${MAIN_DIR}/game_2048_core.c" \
    -o "${BUILD_DIR}/game_2048_core_test"

"${CC}" "${CFLAGS[@]}" \
    "${SCRIPT_DIR}/game_2048_gesture_test.c" \
    "${MAIN_DIR}/game_2048_gesture.c" \
    -o "${BUILD_DIR}/game_2048_gesture_test"

"${CC}" "${CFLAGS[@]}" \
    "${SCRIPT_DIR}/game_2048_storage_test.c" \
    "${MAIN_DIR}/game_2048_storage.c" \
    -o "${BUILD_DIR}/game_2048_storage_test"

"${BUILD_DIR}/game_2048_core_test"
"${BUILD_DIR}/game_2048_gesture_test"
"${BUILD_DIR}/game_2048_storage_test"
