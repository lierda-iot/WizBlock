#!/bin/sh
set -eu

LABEL="com.codex-task-notifier.bridge"
PLIST_PATH="$HOME/Library/LaunchAgents/$LABEL.plist"
CONFIG_DIR="$HOME/Library/Application Support/CodexTaskNotifierDemo"

launchctl bootout "gui/$(id -u)" "$PLIST_PATH" >/dev/null 2>&1 || true
rm -f "$PLIST_PATH"

printf '%s\n' "Bridge LaunchAgent removed: $PLIST_PATH"
printf '%s\n' "State, token and logs were retained: $CONFIG_DIR"
