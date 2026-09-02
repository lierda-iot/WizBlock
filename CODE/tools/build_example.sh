#!/bin/bash
# Example 构建/烧录一体化脚本
# 用法：
#   构建：  ./tools/build_example.sh camera_display_demo
#   干净构建：./tools/build_example.sh camera_display_demo clean
#   烧录：  ./tools/build_example.sh camera_display_demo flash
#
# 前提：从项目根目录执行，或使用绝对路径

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CODE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MIRROR_ROOT="$TEMP/laiwfs300_build/CODE"
PS_SCRIPT="$CODE_ROOT/tools/build_example.ps1"

EXAMPLE="${1:?Usage: $0 <example_name> [clean|flash]}"
ACTION="${2:-build}"

echo "=== Example Build: $EXAMPLE ($ACTION) ==="
echo "  Source: $CODE_ROOT"
echo "  Mirror: $MIRROR_ROOT"

# Step 1: bash mirror
echo "[1/2] Mirroring CODE to ASCII path..."
rm -rf "$MIRROR_ROOT"
cp -r "$CODE_ROOT" "$MIRROR_ROOT"
echo "  mirror done."

# Step 2: PowerShell build/flash
echo "[2/2] Running build_example.ps1..."
case "$ACTION" in
    clean)
        powershell.exe -ExecutionPolicy Bypass -File "$PS_SCRIPT" -Example "$EXAMPLE" -Clean 2>&1 | tail -40
        ;;
    flash)
        powershell.exe -ExecutionPolicy Bypass -File "$PS_SCRIPT" -Example "$EXAMPLE" -Port COM7 2>&1 | tail -40
        ;;
    build)
        powershell.exe -ExecutionPolicy Bypass -File "$PS_SCRIPT" -Example "$EXAMPLE" 2>&1 | tail -40
        ;;
    *)
        echo "Unknown action: $ACTION (use build/clean/flash)"
        exit 1
        ;;
esac
