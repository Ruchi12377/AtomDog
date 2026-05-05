#!/data/data/com.termux/files/usr/bin/bash

PID_FILE="$HOME/AI-server-run/llama-server.pid"
LOG_FILE="$HOME/AI-server-run/llama-server.log"
MODE_FILE="$HOME/AI-server-run/llama-server.mode"

MODEL_DIR="$HOME/models"
LLAMA_BIN="$HOME/llama.cpp-latest/build/bin/llama-server"
PORT="8080"

MODEL_ARG="${1:-qwen}"

resolve_model_path() {
  case "$1" in
    qwen)
      echo "$MODEL_DIR/Qwen_Qwen3.5-9B-Q4_K_M.gguf"
      return 0
      ;;
    gemma|e4b|gemma4-e4b)
      echo "$MODEL_DIR/gemma-4-E4B-it-Q4_K_M.gguf"
      return 0
      ;;
    e2b|gemma4-e2b)
      echo "$MODEL_DIR/gemma-4-E2B-it-Q4_K_M.gguf"
      return 0
      ;;
    e2b-pure|gemma4-e2b-pure)
      echo "$MODEL_DIR/gemma-4-E2B-it-Q4_0_pure.gguf"
      return 0
      ;;
    gemma3-4b)
      echo "$MODEL_DIR/gemma-3-4b-it-Q4_K_M.gguf"
      return 0
      ;;
    gemma3-1b)
      echo "$MODEL_DIR/gemma-3-1b-it-Q4_K_M.gguf"
      return 0
      ;;
  esac

  if [ -f "$1" ]; then
    echo "$1"
    return 0
  fi

  if [ -f "$MODEL_DIR/$1" ]; then
    echo "$MODEL_DIR/$1"
    return 0
  fi

  return 1
}

if [ ! -x "$LLAMA_BIN" ]; then
  echo "llama-server binary not found:"
  echo "  $LLAMA_BIN"
  exit 1
fi

MODEL="$(resolve_model_path "$MODEL_ARG")"
if [ -z "$MODEL" ] || [ ! -f "$MODEL" ]; then
  echo "Model not found: $MODEL_ARG"
  echo
  echo "Examples:"
  echo "  $0 qwen"
  echo "  $0 gemma"
  echo "  $0 e2b"
  echo "  $0 e2b-pure"
  echo "  $0 gemma3-4b"
  echo "  $0 gemma3-1b"
  echo "  $0 gemma-4-E4B-it-Q4_K_M.gguf"
  echo "  $0 /full/path/to/model.gguf"
  exit 1
fi

if [ -f "$PID_FILE" ]; then
  PID="$(cat "$PID_FILE" 2>/dev/null)"
  if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
    CURRENT_MODE="unknown"
    [ -f "$MODE_FILE" ] && CURRENT_MODE="$(cat "$MODE_FILE" 2>/dev/null)"
    echo "llama-server is already running. PID=$PID"
    echo "current mode: $CURRENT_MODE"
    echo "Stop it first:"
    echo "  $HOME/AI-server-run/stop_llama.sh"
    exit 0
  else
    echo "Removing stale PID file."
    rm -f "$PID_FILE"
  fi
fi

termux-wake-lock 2>/dev/null

nohup "$LLAMA_BIN" \
  -m "$MODEL" \
  --reasoning off \
  --reasoning-budget 0 \
  --temp 0.7 --top-p 0.8 --top-k 20 --min-p 0 \
  -c 2048 -ngl 0 \
  --host 0.0.0.0 --port "$PORT" \
  > "$LOG_FILE" 2>&1 < /dev/null &

echo $! > "$PID_FILE"
echo "text" > "$MODE_FILE"
sleep 2

PID="$(cat "$PID_FILE")"
if kill -0 "$PID" 2>/dev/null; then
  echo "llama-server started. PID=$PID"
  echo "mode  : text"
  echo "binary: $LLAMA_BIN"
  echo "model : $MODEL"
  echo "port  : $PORT"
  echo "log   : $LOG_FILE"
else
  echo "Failed to start llama-server."
  echo "----- last 50 log lines -----"
  [ -f "$LOG_FILE" ] && tail -n 50 "$LOG_FILE"
  rm -f "$PID_FILE" "$MODE_FILE"
  exit 1
fi
