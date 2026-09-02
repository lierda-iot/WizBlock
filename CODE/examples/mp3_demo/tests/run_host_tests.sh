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
    "${SCRIPT_DIR}/mp3_catalog_test.c" \
    "${MAIN_DIR}/mp3_catalog.c" \
    -o "${BUILD_DIR}/mp3_catalog_test"

"${CC}" "${CFLAGS[@]}" \
    "${SCRIPT_DIR}/mp3_lrc_test.c" \
    "${MAIN_DIR}/mp3_lrc.c" \
    -o "${BUILD_DIR}/mp3_lrc_test"

"${CC}" "${CFLAGS[@]}" \
    "${SCRIPT_DIR}/mp3_progress_test.c" \
    "${MAIN_DIR}/mp3_progress.c" \
    -o "${BUILD_DIR}/mp3_progress_test"

"${BUILD_DIR}/mp3_catalog_test"
"${BUILD_DIR}/mp3_lrc_test"
"${BUILD_DIR}/mp3_progress_test"
