#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAIN_DIR="${SCRIPT_DIR}/../main"
BUILD_DIR="${SCRIPT_DIR}/.build"
CC="${CC:-cc}"
CFLAGS=(-std=c11 -Wall -Wextra -Werror -pedantic -I"${MAIN_DIR}")
VENDOR_CFLAGS=(-Wno-unused-parameter -Wno-unused-function -Wno-sign-compare)

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

"${CC}" "${CFLAGS[@]}" \
    "${SCRIPT_DIR}/ofdm_crc_test.c" \
    "${MAIN_DIR}/ofdm_crc.c" \
    -o "${BUILD_DIR}/ofdm_crc_test"

"${CC}" "${CFLAGS[@]}" \
    "${SCRIPT_DIR}/ofdm_utf8_test.c" \
    "${MAIN_DIR}/ofdm_utf8.c" \
    -o "${BUILD_DIR}/ofdm_utf8_test"

"${CC}" "${CFLAGS[@]}" \
    "${SCRIPT_DIR}/ofdm_frame_test.c" \
    "${MAIN_DIR}/ofdm_crc.c" \
    "${MAIN_DIR}/ofdm_frame.c" \
    "${MAIN_DIR}/ofdm_utf8.c" \
    -o "${BUILD_DIR}/ofdm_frame_test"

"${CC}" "${CFLAGS[@]}" \
    "${VENDOR_CFLAGS[@]}" \
    "${SCRIPT_DIR}/ofdm_fec_test.c" \
    "${MAIN_DIR}/ofdm_crc.c" \
    "${MAIN_DIR}/ofdm_fec.c" \
    "${SCRIPT_DIR}/../components/libcorrect/src/reed-solomon/decode.c" \
    "${SCRIPT_DIR}/../components/libcorrect/src/reed-solomon/encode.c" \
    "${SCRIPT_DIR}/../components/libcorrect/src/reed-solomon/polynomial.c" \
    "${SCRIPT_DIR}/../components/libcorrect/src/reed-solomon/reed-solomon.c" \
    -I"${SCRIPT_DIR}/../components/libcorrect/include" \
    -o "${BUILD_DIR}/ofdm_fec_test"

"${CC}" "${CFLAGS[@]}" \
    "${VENDOR_CFLAGS[@]}" \
    "${SCRIPT_DIR}/ofdm_sync_test.c" \
    "${MAIN_DIR}/ofdm_phy.c" \
    "${MAIN_DIR}/ofdm_sync.c" \
    "${SCRIPT_DIR}/../components/kissfft/src/kiss_fft.c" \
    -I"${SCRIPT_DIR}/../components/kissfft/include" \
    -lm \
    -o "${BUILD_DIR}/ofdm_sync_test"

"${CC}" "${CFLAGS[@]}" \
    "${VENDOR_CFLAGS[@]}" \
    "${SCRIPT_DIR}/ofdm_phy_test.c" \
    "${MAIN_DIR}/ofdm_crc.c" \
    "${MAIN_DIR}/ofdm_fec.c" \
    "${MAIN_DIR}/ofdm_frame.c" \
    "${MAIN_DIR}/ofdm_phy.c" \
    "${MAIN_DIR}/ofdm_utf8.c" \
    "${SCRIPT_DIR}/../components/libcorrect/src/reed-solomon/decode.c" \
    "${SCRIPT_DIR}/../components/libcorrect/src/reed-solomon/encode.c" \
    "${SCRIPT_DIR}/../components/libcorrect/src/reed-solomon/polynomial.c" \
    "${SCRIPT_DIR}/../components/libcorrect/src/reed-solomon/reed-solomon.c" \
    "${SCRIPT_DIR}/../components/kissfft/src/kiss_fft.c" \
    -I"${SCRIPT_DIR}/../components/libcorrect/include" \
    -I"${SCRIPT_DIR}/../components/kissfft/include" \
    -lm \
    -o "${BUILD_DIR}/ofdm_phy_test"

"${CC}" "${CFLAGS[@]}" \
    "${VENDOR_CFLAGS[@]}" \
    "${SCRIPT_DIR}/ofdm_calibration_test.c" \
    "${MAIN_DIR}/ofdm_calibration.c" \
    "${MAIN_DIR}/ofdm_crc.c" \
    "${MAIN_DIR}/ofdm_fec.c" \
    "${MAIN_DIR}/ofdm_frame.c" \
    "${MAIN_DIR}/ofdm_phy.c" \
    "${MAIN_DIR}/ofdm_utf8.c" \
    "${SCRIPT_DIR}/../components/libcorrect/src/reed-solomon/decode.c" \
    "${SCRIPT_DIR}/../components/libcorrect/src/reed-solomon/encode.c" \
    "${SCRIPT_DIR}/../components/libcorrect/src/reed-solomon/polynomial.c" \
    "${SCRIPT_DIR}/../components/libcorrect/src/reed-solomon/reed-solomon.c" \
    "${SCRIPT_DIR}/../components/kissfft/src/kiss_fft.c" \
    -I"${SCRIPT_DIR}/../components/libcorrect/include" \
    -I"${SCRIPT_DIR}/../components/kissfft/include" \
    -lm \
    -o "${BUILD_DIR}/ofdm_calibration_test"

"${BUILD_DIR}/ofdm_crc_test"
"${BUILD_DIR}/ofdm_utf8_test"
"${BUILD_DIR}/ofdm_frame_test"
"${BUILD_DIR}/ofdm_fec_test"
"${BUILD_DIR}/ofdm_phy_test"
"${BUILD_DIR}/ofdm_sync_test"
"${BUILD_DIR}/ofdm_calibration_test"
