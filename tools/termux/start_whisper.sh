#!/data/data/com.termux/files/usr/bin/bash

PID_FILE="$HOME/AI-server-run/whisper-server.pid"
LOG_FILE="$HOME/AI-server-run/whisper-server.log"
WHISPER_DIR="$HOME/whisper.cpp"
WHISPER_BIN="$WHISPER_DIR/build/bin/whisper-server"
MODEL_PATH="$WHISPER_DIR/models/ggml-small.bin"
PORT="8081"

if [ ! -x "$WHISPER_BIN" ]; then
  echo "whisper-server binary not found:"
  echo "  $WHISPER_BIN"
  exit 1
fi

if [ ! -f "$MODEL_PATH" ]; then
  echo "Whisper model not found:"
  echo "  $MODEL_PATH"
  exit 1
fi

if [ -f "$PID_FILE" ]; then
  PID="$(cat "$PID_FILE")"
  if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
    echo "whisper-server is already running. PID=$PID"
    echo "Stop it first:"
    echo "  $HOME/AI-server-run/stop_whisper.sh"
    exit 0
  else
    echo "Removing stale PID file."
    rm -f "$PID_FILE"
  fi
fi

termux-wake-lock 2>/dev/null

nohup "$WHISPER_BIN" \
  -m "$MODEL_PATH" \
  --host 0.0.0.0 \
  --port "$PORT" \
  > "$LOG_FILE" 2>&1 < /dev/null &

echo $! > "$PID_FILE"
sleep 2

PID="$(cat "$PID_FILE")"
if kill -0 "$PID" 2>/dev/null; then
  echo "whisper-server started. PID=$PID"
  echo "binary: $WHISPER_BIN"
  echo "model : $MODEL_PATH"
  echo "log   : $LOG_FILE"
else
  echo "Failed to start whisper-server."
  echo "----- last 50 log lines -----"
  [ -f "$LOG_FILE" ] && tail -n 50 "$LOG_FILE"
  rm -f "$PID_FILE"
  exit 1
fi
