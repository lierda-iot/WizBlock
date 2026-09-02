#!/bin/bash
#
# macOS bash flash script for ai_companion_robot
# Adapated from idf_flash.ps1 for macOS environment
#
# Usage: ./tools/idf_flash.sh -p <port>
#   -p: Serial port (e.g. /dev/tty.usbserial-1410 or /dev/ttyUSB0)
#
# Requires: ESP-IDF environment already activated via:
#   source ~/.espressif/v5.5.4/esp-idf/export.sh

MIRROR_ROOT="/tmp/laiwfs300_build/CODE"
PORT=""

while getopts "p:h" opt; do
  case $opt in
    p) PORT="$OPTARG" ;;
    h) echo "Usage: $0 -p <serial_port>" && exit 0 ;;
    *) echo "Invalid option" && exit 1 ;;
  esac
done

if [ -z "$PORT" ]; then
  echo "ERROR: Please specify serial port with -p"
  echo "Example: $0 -p /dev/tty.usbserial-1410"
  exit 1
fi

# Check that idf.py is available
if ! command -v idf.py &> /dev/null; then
  echo "ERROR: idf.py not found in PATH"
  echo "Please activate ESP-IDF environment first:"
  echo "  source ~/.espressif/v5.5.4/esp-idf/export.sh"
  exit 1
fi

# Check required paths
for path in "$MIRROR_ROOT" "$MIRROR_ROOT/build"; do
  if [ ! -e "$path" ]; then
    echo "ERROR: Required path missing: $path"
    echo "Run tools/idf_build.sh first to build the firmware"
    exit 1
  fi
done

# Change to mirror directory and flash
cd "$MIRROR_ROOT" || exit 1
idf.py -p "$PORT" flash
exit $?
