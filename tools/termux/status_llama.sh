#!/data/data/com.termux/files/usr/bin/bash

PID_FILE="$HOME/AI-server-run/llama-server.pid"
LOG_FILE="$HOME/AI-server-run/llama-server.log"
MODE_FILE="$HOME/AI-server-run/llama-server.mode"
LLAMA_BIN="$HOME/llama.cpp-latest/build/bin/llama-server"
PORT="8080"

echo "=== llama-server status ==="
echo "binary: $LLAMA_BIN"
echo "port  : $PORT"

if [ -x "$LLAMA_BIN" ]; then
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

MODE="unknown"
[ -f "$MODE_FILE" ] && MODE="$(cat "$MODE_FILE" 2>/dev/null)"

CURRENT_MODEL=""
CURRENT_MMPROJ=""

if [ "$RUNNING" -eq 1 ] && [ -r "/proc/$PID/cmdline" ]; then
  ARGS="$(tr '\0' '\n' < "/proc/$PID/cmdline" 2>/dev/null)"
  CURRENT_MODEL="$(printf '%s\n' "$ARGS" | awk 'prev=="-m"{print; exit}{prev=$0}')"
  CURRENT_MMPROJ="$(printf '%s\n' "$ARGS" | awk 'prev=="--mmproj"{print; exit}{prev=$0}')"
fi

echo
echo "mode: $MODE"

if [ "$RUNNING" -eq 1 ]; then
  echo "current_model : ${CURRENT_MODEL:-"(unknown)"}"
  if [ -n "$CURRENT_MMPROJ" ]; then
    echo "current_mmproj: $CURRENT_MMPROJ"
  else
    echo "current_mmproj: (none)"
  fi
else
  echo "current_model : (stopped)"
  echo "current_mmproj: (stopped)"
fi

echo
echo "HTTP check:"
curl -s "http://127.0.0.1:${PORT}/health" 2>/dev/null || echo "No response"

echo
echo "Last 20 log lines:"
if [ -f "$LOG_FILE" ]; then
  tail -n 20 "$LOG_FILE"
else
  echo "Log file not found."
fi
