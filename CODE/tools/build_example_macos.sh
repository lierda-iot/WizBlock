#!/bin/bash
# macOS Example 构建/烧录脚本
# 用法：
#   构建：  bash ./tools/build_example_macos.sh display_demo
#   干净构建：bash ./tools/build_example_macos.sh display_demo clean
#   烧录：  bash ./tools/build_example_macos.sh display_demo flash -p /dev/cu.usbserial-1410

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: bash ./tools/build_example_macos.sh <example_name> [build|clean|flash] [-p <serial_port>]

Examples:
  bash ./tools/build_example_macos.sh display_demo
  bash ./tools/build_example_macos.sh display_demo clean
  bash ./tools/build_example_macos.sh display_demo flash -p /dev/cu.usbserial-1410
EOF
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CODE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MIRROR_ROOT="/tmp/laiwfs300_build/CODE"
IDF_EXPORT="$HOME/.espressif/v5.5.4/esp-idf/export.sh"
CERTIFI_BUNDLE="$HOME/.espressif/tools/python/v5.5.4/venv/lib/python3.14/site-packages/certifi/cacert.pem"

if [[ $# -lt 1 ]]; then
    usage
    exit 1
fi

case "${1:-}" in
    -h|--help)
        usage
        exit 0
        ;;
esac

EXAMPLE="$1"
ACTION="build"
PORT="${ESP_PORT:-}"
shift

if [[ $# -gt 0 ]]; then
    case "$1" in
        build|clean|flash)
            ACTION="$1"
            shift
            ;;
    esac
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--port)
            if [[ $# -lt 2 ]]; then
                echo "ERROR: Missing serial port after $1"
                exit 1
            fi
            PORT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "ERROR: Unknown argument: $1"
            usage
            exit 1
            ;;
    esac
done

if [[ ! -d "$CODE_ROOT/examples/$EXAMPLE" ]]; then
    echo "ERROR: Example not found: $CODE_ROOT/examples/$EXAMPLE"
    exit 1
fi

if [[ ! -f "$IDF_EXPORT" ]]; then
    echo "ERROR: ESP-IDF export script not found: $IDF_EXPORT"
    exit 1
fi

if [[ -f "$CERTIFI_BUNDLE" ]]; then
    export SSL_CERT_FILE="$CERTIFI_BUNDLE"
fi

echo "=== macOS Example Build: $EXAMPLE ($ACTION) ==="
echo "  Source: $CODE_ROOT"
echo "  Mirror: $MIRROR_ROOT"

echo "[1/3] Activating ESP-IDF..."
# shellcheck disable=SC1090
source "$IDF_EXPORT"

if ! command -v idf.py >/dev/null 2>&1; then
    echo "ERROR: idf.py not found after sourcing $IDF_EXPORT"
    exit 1
fi

if ! command -v rsync >/dev/null 2>&1; then
    echo "ERROR: rsync not found in PATH"
    exit 1
fi

echo "[2/3] Mirroring CODE to ASCII path..."
mkdir -p "$(dirname "$MIRROR_ROOT")"
rsync -a --delete \
    --exclude="/build/" \
    --exclude="/sdkconfig" \
    --exclude="/sdkconfig.old" \
    "$CODE_ROOT/" "$MIRROR_ROOT/"

MIRROR_EXAMPLE_DIR="$MIRROR_ROOT/examples/$EXAMPLE"
if [[ ! -d "$MIRROR_EXAMPLE_DIR" ]]; then
    echo "ERROR: Mirrored example directory missing: $MIRROR_EXAMPLE_DIR"
    exit 1
fi

echo "[3/3] Running idf.py in $MIRROR_EXAMPLE_DIR..."
pushd "$MIRROR_EXAMPLE_DIR" >/dev/null
case "$ACTION" in
    build|clean)
        # Example 改码后默认做 fullclean build，避免镜像时间戳导致增量构建漏编译。
        idf.py fullclean build
        ;;
    flash)
        if [[ -z "$PORT" ]]; then
            echo "ERROR: macOS flash requires -p <serial_port>"
            exit 1
        fi
        idf.py fullclean build
        idf.py -p "$PORT" erase-flash flash
        ;;
    *)
        echo "ERROR: Unknown action: $ACTION (use build/clean/flash)"
        exit 1
        ;;
esac
popd >/dev/null
