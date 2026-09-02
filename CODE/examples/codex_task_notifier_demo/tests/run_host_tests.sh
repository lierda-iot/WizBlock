#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR="${TMPDIR:-/tmp}/codex_task_notifier_tests"
CC_BIN=${CC:-cc}

mkdir -p "$BUILD_DIR"

$CC_BIN -std=c11 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT_DIR/main" -I"$ROOT_DIR/main/third_party" \
    "$ROOT_DIR/tests/notifier_model_test.c" \
    "$ROOT_DIR/main/notifier_model.c" \
    -o "$BUILD_DIR/notifier_model_test"
"$BUILD_DIR/notifier_model_test"

$CC_BIN -std=c11 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT_DIR/main" -I"$ROOT_DIR/main/third_party" \
    "$ROOT_DIR/tests/notifier_protocol_test.c" \
    "$ROOT_DIR/main/notifier_protocol.c" \
    "$ROOT_DIR/main/third_party/jsmn.c" \
    -o "$BUILD_DIR/notifier_protocol_test"
"$BUILD_DIR/notifier_protocol_test"

$CC_BIN -std=c11 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT_DIR/main" \
    "$ROOT_DIR/tests/notifier_wifi_config_test.c" \
    "$ROOT_DIR/main/notifier_wifi_config.c" \
    -o "$BUILD_DIR/notifier_wifi_config_test"
"$BUILD_DIR/notifier_wifi_config_test"

python3 "$ROOT_DIR/tests/verify_notifier_font.py" \
    "$ROOT_DIR/main/notifier_noto_sans_sc_16.bin"
