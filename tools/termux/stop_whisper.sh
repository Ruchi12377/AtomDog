#!/data/data/com.termux/files/usr/bin/bash

PID_FILE="$HOME/AI-server-run/whisper-server.pid"
WHISPER_BIN="$HOME/whisper.cpp/build/bin/whisper-server"
PORT="8081"

find_whisper_pids() {
  for d in /proc/[0-9]*; do
    [ -r "$d/cmdline" ] || continue
    CMDLINE="$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null)"
    case "$CMDLINE" in
      *"$WHISPER_BIN"*' --port '"$PORT"*|*"$WHISPER_BIN"*' --host 0.0.0.0 --port '"$PORT"*)
        echo "${d##*/}"
        ;;
    esac
  done
}

TARGET_PIDS=""

if [ -f "$PID_FILE" ]; then
  PID="$(cat "$PID_FILE" 2>/dev/null)"
  if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
    TARGET_PIDS="$PID"
  else
    echo "PID file exists but process is not running. Removing stale PID file."
    rm -f "$PID_FILE"
  fi
fi

if [ -z "$TARGET_PIDS" ]; then
  FOUND_PIDS="$(find_whisper_pids | tr '\n' ' ' | sed 's/[[:space:]]\+$//')"
  if [ -n "$FOUND_PIDS" ]; then
    TARGET_PIDS="$FOUND_PIDS"
    echo "Found running whisper-server process(es) without usable PID file: $TARGET_PIDS"
  fi
fi

if [ -z "$TARGET_PIDS" ]; then
  echo "No running whisper-server process found."
  termux-wake-unlock 2>/dev/null
  exit 0
fi

echo "Stopping whisper-server..."
for PID in $TARGET_PIDS; do
  echo "  send SIGTERM to PID=$PID"
  kill "$PID" 2>/dev/null
done

sleep 2

REMAINING=""
for PID in $TARGET_PIDS; do
  if kill -0 "$PID" 2>/dev/null; then
    REMAINING="$REMAINING $PID"
  fi
done
REMAINING="$(echo "$REMAINING" | sed 's/^ *//')"

if [ -n "$REMAINING" ]; then
  echo "Still alive after SIGTERM. Sending SIGKILL to:$REMAINING"
  for PID in $REMAINING; do
    kill -9 "$PID" 2>/dev/null
  done
  sleep 1
fi

FINAL_REMAINING=""
for PID in $TARGET_PIDS; do
  if kill -0 "$PID" 2>/dev/null; then
    FINAL_REMAINING="$FINAL_REMAINING $PID"
  fi
done
FINAL_REMAINING="$(echo "$FINAL_REMAINING" | sed 's/^ *//')"

rm -f "$PID_FILE"
termux-wake-unlock 2>/dev/null

if [ -n "$FINAL_REMAINING" ]; then
  echo "Failed to stop PID(s):$FINAL_REMAINING"
  exit 1
fi

echo "whisper-server stopped."
