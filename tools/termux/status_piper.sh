#!/data/data/com.termux/files/usr/bin/bash

PID_FILE="$HOME/AI-server-run/piper-server.pid"
WRAPPER_PID_FILE="$HOME/AI-server-run/piper-wrapper.pid"
LOG_FILE="$HOME/AI-server-run/piper-server.log"
PORT="5000"

echo "=== piper server status ==="
echo "port: $PORT"
echo

RUNNING=0

if [ -f "$PID_FILE" ]; then
  PID="$(cat "$PID_FILE")"
  echo "uvicorn_pid_file: $PID_FILE"
  echo "uvicorn_pid     : $PID"
  if kill -0 "$PID" 2>/dev/null; then
    echo "uvicorn_process : RUNNING"
    RUNNING=1
  else
    echo "uvicorn_process : NOT RUNNING (stale pid file)"
  fi
else
  echo "uvicorn_pid_file: NOT FOUND"
  echo "uvicorn_process : NOT RUNNING"
fi

echo

if [ -f "$WRAPPER_PID_FILE" ]; then
  WPID="$(cat "$WRAPPER_PID_FILE")"
  echo "wrapper_pid_file: $WRAPPER_PID_FILE"
  echo "wrapper_pid     : $WPID"
  if kill -0 "$WPID" 2>/dev/null; then
    echo "wrapper_process : RUNNING"
  else
    echo "wrapper_process : NOT RUNNING"
  fi
else
  echo "wrapper_pid_file: NOT FOUND"
  echo "wrapper_process : NOT RUNNING"
fi

echo
if [ "$RUNNING" -eq 1 ]; then
  echo "current_service : uvicorn tts_server:app"
else
  echo "current_service : (stopped)"
fi

echo
echo "HTTP check (/health):"
curl -s http://127.0.0.1:${PORT}/health 2>/dev/null || echo "No response"

echo
echo "Last 20 log lines:"
if [ -f "$LOG_FILE" ]; then
  tail -n 20 "$LOG_FILE"
else
  echo "Log file not found."
fi
