#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BRIDGE_DIR="$ROOT_DIR/bridge"
ENV_LOCAL="$BRIDGE_DIR/.env.local"
CONFIG_DIR="$HOME/Library/Application Support/CodexTaskNotifierDemo"
RUNTIME_DIR="$CONFIG_DIR/runtime"
RUNTIME_NEXT="$CONFIG_DIR/runtime.new.$$"
VENV_DIR="$CONFIG_DIR/venv"
RUNNER_PATH="$CONFIG_DIR/run-bridge.sh"
LAUNCH_AGENTS_DIR="$HOME/Library/LaunchAgents"
PLIST_PATH="$LAUNCH_AGENTS_DIR/com.codex-task-notifier.bridge.plist"
LABEL="com.codex-task-notifier.bridge"

umask 077

generate_token() {
    if command -v openssl >/dev/null 2>&1; then
        openssl rand -hex 32
        return
    fi
    python3 -c 'import secrets; print(secrets.token_hex(32))'
}

if [ ! -f "$ENV_LOCAL" ]; then
    token=$(generate_token)
    printf 'CODEX_NOTIFIER_TOKEN=%s\n' "$token" > "$ENV_LOCAL"
fi

set -a
# The generated value is shell-safe hex; manually edited files must remain valid shell syntax.
. "$ENV_LOCAL"
set +a

PYTHONPATH="$BRIDGE_DIR/src" python3 -c \
    'import os; from codex_task_bridge.__main__ import validate_token; validate_token(os.environ["CODEX_NOTIFIER_TOKEN"])'

mkdir -p "$CONFIG_DIR" "$LAUNCH_AGENTS_DIR"
rm -rf "$RUNTIME_NEXT"
mkdir -p "$RUNTIME_NEXT"
cp -R "$BRIDGE_DIR/src" "$RUNTIME_NEXT/src"
rm -rf "$RUNTIME_DIR"
mv "$RUNTIME_NEXT" "$RUNTIME_DIR"
cp "$ENV_LOCAL" "$CONFIG_DIR/.env"
chmod 600 "$CONFIG_DIR/.env"

if [ ! -x "$VENV_DIR/bin/python" ]; then
    python3 -m venv "$VENV_DIR"
fi

cat > "$RUNNER_PATH" <<RUNNER
#!/bin/sh
set -eu
set -a
. "$CONFIG_DIR/.env"
set +a
PYTHONPATH="$RUNTIME_DIR/src" exec "$VENV_DIR/bin/python" -m codex_task_bridge \\
    --host 0.0.0.0 \\
    --port 8765 \\
    --state-file "$CONFIG_DIR/bridge-state.json"
RUNNER
chmod 700 "$RUNNER_PATH"

cat > "$PLIST_PATH" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>/bin/sh</string>
    <string>$RUNNER_PATH</string>
  </array>
  <key>WorkingDirectory</key>
  <string>$CONFIG_DIR</string>
  <key>EnvironmentVariables</key>
  <dict>
    <key>PATH</key>
    <string>/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin</string>
  </dict>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>ThrottleInterval</key>
  <integer>5</integer>
  <key>StandardOutPath</key>
  <string>$CONFIG_DIR/bridge.log</string>
  <key>StandardErrorPath</key>
  <string>$CONFIG_DIR/bridge.err.log</string>
</dict>
</plist>
PLIST
chmod 600 "$PLIST_PATH"

launchctl bootout "gui/$(id -u)" "$PLIST_PATH" >/dev/null 2>&1 || true
launchctl bootstrap "gui/$(id -u)" "$PLIST_PATH"
launchctl kickstart -k "gui/$(id -u)/$LABEL"

printf '%s\n' "Bridge installed: $PLIST_PATH"
printf '%s\n' "Runtime data: $CONFIG_DIR"
printf '%s\n' "Local token: $ENV_LOCAL"
