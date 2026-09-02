#!/bin/bash
#
# macOS bash build script for ai_companion_robot
# Adapated from idf_build.ps1 for macOS environment
#
# Usage: ./tools/idf_build.sh [-c]
#   -c: Clean build (remove build and sdkconfig)
#
# Requires: ESP-IDF environment already activated via:
#   source ~/.espressif/v5.5.4/esp-idf/export.sh

MIRROR_ROOT="/tmp/laiwfs300_build/CODE"
CLEAN=false

while getopts "ch" opt; do
  case $opt in
    c) CLEAN=true ;;
    h) echo "Usage: $0 [-c]" && exit 0 ;;
    *) echo "Invalid option" && exit 1 ;;
  esac
done

# Get code root (directory of this script/..)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CODE_ROOT="$(dirname "$SCRIPT_DIR")"

# Check that idf.py is available
if ! command -v idf.py &> /dev/null; then
  echo "ERROR: idf.py not found in PATH"
  echo "Please activate ESP-IDF environment first:"
  echo "  source ~/.espressif/v5.5.4/esp-idf/export.sh"
  exit 1
fi

# Create mirror parent directory
mkdir -p "$(dirname "$MIRROR_ROOT")"

# Mirror code to temp directory (to avoid Chinese path issues)
# rsync equivalent to robocopy /MIR
# Exclude build and sdkconfig
rsync -a --delete \
  --exclude="/build/" \
  --exclude="/sdkconfig" \
  --exclude="/sdkconfig.old" \
  "$CODE_ROOT/" "$MIRROR_ROOT/"

if [ $? -ne 0 ]; then
  echo "ERROR: rsync mirror failed"
  exit 1
fi

if [ ! -f "$MIRROR_ROOT/CMakeLists.txt" ]; then
  echo "ERROR: Build mirror is invalid, CMakeLists.txt not found at $MIRROR_ROOT"
  exit 1
fi

# Clean if requested
if [ "$CLEAN" = true ]; then
  BUILD_PATH="$MIRROR_ROOT/build"
  if [ -d "$BUILD_PATH" ]; then
    rm -rf "$BUILD_PATH"
  fi
  for config in sdkconfig sdkconfig.old; do
    config_path="$MIRROR_ROOT/$config"
    if [ -f "$config_path" ]; then
      rm -f "$config_path"
    fi
  done
fi

# Change to mirror directory and build
cd "$MIRROR_ROOT" || exit 1
idf.py build
exit $?
