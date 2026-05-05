#!/data/data/com.termux/files/usr/bin/bash

PID_FILE="$HOME/AI-server-run/whisper-server.pid"
LOG_FILE="$HOME/AI-server-run/whisper-server.log"
WHISPER_BIN="$HOME/whisper.cpp/build/bin/whisper-server"

echo "=== whisper-server status ==="
echo "binary: $WHISPER_BIN"

if [ -x "$WHISPER_BIN" ]; then
  echo "binary_status: OK"
else
  echo "binary_status: NOT FOUND"
fi

echo

RUNNING=0
PID=""

if [ -f "$PID_FILE" ]; then
  PID="$(cat "$PID_FILE")"
  echo "pid_file: $PID_FILE"
  echo "pid     : $PID"

  if kill -0 "$PID" 2>/dev/null; then
    echo "process : RUNNING"
    RUNNING=1
  else
    echo "process : NOT RUNNING (stale pid file)"
  fi
else
  echo "pid_file: NOT FOUND"
  echo "process : NOT RUNNING"
fi

LAST_MODEL=""
if [ -f "$LOG_FILE" ]; then
  MODEL_LINE="$(grep -m 1 "whisper_init_from_file_with_params_no_state:" "$LOG_FILE")"
  if [ -n "$MODEL_LINE" ]; then
    LAST_MODEL="$(printf '%s\n' "$MODEL_LINE" | sed -E 's/.*loading model from '\''([^'\'']+)'\''.*/\1/')"
  fi
fi

echo
if [ "$RUNNING" -eq 1 ]; then
  if [ -n "$LAST_MODEL" ]; then
    echo "current_model    : $LAST_MODEL"
  else
    echo "current_model    : (unknown)"
  fi
else
  echo "current_model    : (stopped)"
fi

if [ -n "$LAST_MODEL" ]; then
  echo "last_logged_model: $LAST_MODEL"
else
  echo "last_logged_model: (not found)"
fi

echo
echo "HTTP check:"
curl -s http://127.0.0.1:8081 2>/dev/null || echo "No response"

echo
echo "Last 20 log lines:"
if [ -f "$LOG_FILE" ]; then
  tail -n 20 "$LOG_FILE"
else
  echo "Log file not found."
fi
