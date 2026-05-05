#!/data/data/com.termux/files/usr/bin/bash

PID_FILE="$HOME/AI-server-run/llama-server.pid"
LOG_FILE="$HOME/AI-server-run/llama-server.log"
MODE_FILE="$HOME/AI-server-run/llama-server.mode"

MODEL_DIR="$HOME/models"

# OpenCL 対応ビルド
# 2026-05-03 時点の最新版ビルド。
# Gemma 4 E4B では、旧Vision高速構成:
#   model  : gemma-4-E4B-it-Q4_0.gguf
#   mmproj : mmproj-gemma-4-E4B-F16.gguf
#   -ngl all
#   --no-mmproj-offload なし
# が、画像説明・音声Direct Replyの両方で成功したため、gemmaの既定にする。
LLAMA_ROOT="$HOME/llama-opencl-latest-20260503"
LLAMA_BIN="$LLAMA_ROOT/build/bin/llama-server"
LLAMA_LD="/system/lib64:/vendor/lib64:$LLAMA_ROOT/build/src:$LLAMA_ROOT/build/ggml/src:$PREFIX/lib"

PORT="8080"

# 既定を高速・両対応の Gemma 4 E4B にする
MODEL_ARG="${1:-gemma}"

resolve_vlm_pair() {
  case "$1" in
    gemma|gemma-q40|e4b-q40|gemma4-e4b-q40)
      # 本命構成:
      #   画像説明: 高速
      #   音声Direct Reply: 成功
      echo "$MODEL_DIR/gemma-4-E4B-it-Q4_0.gguf|$MODEL_DIR/mmproj-gemma-4-E4B-F16.gguf"
      return 0
      ;;

    e2b|gemma-e2b|gemma4-e2b|gemma4-e2b-q40)
      # 参考: E2Bを試す場合
      echo "$MODEL_DIR/gemma-4-E2B-it-Q4_0_pure.gguf|$MODEL_DIR/mmproj-gemma-4-E2B-BF16.gguf"
      return 0
      ;;

    qwen|qwen-pure|qwen-q40|qwen-q4_0)
      echo "$MODEL_DIR/Qwen_Qwen3.5-9B-Q4_0_pure.gguf|$MODEL_DIR/mmproj-Qwen3.5-9B-F16.gguf"
      return 0
      ;;

    qwen4b|qwen4b-q40|qwen4b-q4_0)
      echo "$MODEL_DIR/Qwen3.5-4B-Q4_0.gguf|$MODEL_DIR/Qwen3.5-4B-mmproj-F16.gguf"
      return 0
      ;;
  esac
  return 1
}

if [ ! -x "$LLAMA_BIN" ]; then
  echo "llama-server binary not found:"
  echo "  $LLAMA_BIN"
  exit 1
fi

PAIR="$(resolve_vlm_pair "$MODEL_ARG")"
if [ -z "$PAIR" ]; then
  echo "Unknown VLM preset: $MODEL_ARG"
  echo
  echo "Examples:"
  echo "  $0 gemma    # Gemma 4 E4B Q4_0 + fast F16 mmproj / image + audio"
  echo "  $0 e2b      # Gemma 4 E2B Q4_0_pure"
  echo "  $0 qwen     # Qwen3.5-9B Q4_0_pure"
  echo "  $0 qwen4b   # Qwen3.5-4B Q4_0"
  exit 1
fi

MODEL="${PAIR%%|*}"
MMPROJ="${PAIR##*|}"

if [ ! -f "$MODEL" ]; then
  echo "Model not found:"
  echo "  $MODEL"
  exit 1
fi

if [ ! -f "$MMPROJ" ]; then
  echo "mmproj not found:"
  echo "  $MMPROJ"
  exit 1
fi

CTX=2048
BACKEND_MODE="gemma-fast"
EXTRA_ARGS=()
NGL_ARGS=(-ngl all)
JINJA_ARGS=()

case "$MODEL_ARG" in
  gemma|gemma-q40|e4b-q40|gemma4-e4b-q40)
    # 本命構成:
    #   旧sh相当の高速Vision構成。
    #   --no-mmproj-offload は付けない。
    #   --device GPUOpenCL も付けない。
    #   -ngl all は付ける。
    #   --jinja は旧shに合わせて付けない。
    #
    # 実測:
    #   image slice encoded 約6.35秒
    #   total 約13.5秒
    #   音声Direct Replyも成功。
    CTX=2048
    BACKEND_MODE="gemma-fast"
    EXTRA_ARGS=()
    NGL_ARGS=(-ngl all)
    JINJA_ARGS=()
    ;;

  e2b|gemma-e2b|gemma4-e2b|gemma4-e2b-q40)
    # E2B参考構成
    CTX=2048
    BACKEND_MODE="e2b"
    EXTRA_ARGS=()
    NGL_ARGS=(-ngl all)
    JINJA_ARGS=()
    ;;

  qwen|qwen-pure|qwen-q40|qwen-q4_0|qwen4b|qwen4b-q40|qwen4b-q4_0)
    # Qwen系VLM OpenCL mode
    CTX=4096
    BACKEND_MODE="qwen-opencl"
    EXTRA_ARGS+=(--device GPUOpenCL --no-mmproj-offload --fit off)
    NGL_ARGS=(-ngl all)
    JINJA_ARGS=(--jinja)
    ;;
esac

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

# 前回ログと混ざらないように起動ごとにログを作り直す
: > "$LOG_FILE"

nohup env LD_LIBRARY_PATH="$LLAMA_LD" \
  "$LLAMA_BIN" \
  "${EXTRA_ARGS[@]}" \
  -m "$MODEL" \
  --mmproj "$MMPROJ" \
  "${JINJA_ARGS[@]}" \
  --reasoning off \
  --reasoning-budget 0 \
  --temp 0.7 --top-p 0.8 --top-k 20 --min-p 0 \
  -c "$CTX" \
  "${NGL_ARGS[@]}" \
  -fa off \
  --host 0.0.0.0 --port "$PORT" \
  > "$LOG_FILE" 2>&1 < /dev/null &

echo $! > "$PID_FILE"
echo "vlm:$MODEL_ARG backend:$BACKEND_MODE" > "$MODE_FILE"
sleep 3

PID="$(cat "$PID_FILE")"
if kill -0 "$PID" 2>/dev/null; then
  EXTRA_DISPLAY="${EXTRA_ARGS[*]}"
  NGL_DISPLAY="${NGL_ARGS[*]}"
  JINJA_DISPLAY="${JINJA_ARGS[*]}"
  [ -z "$EXTRA_DISPLAY" ] && EXTRA_DISPLAY="(none)"
  [ -z "$NGL_DISPLAY" ] && NGL_DISPLAY="(none)"
  [ -z "$JINJA_DISPLAY" ] && JINJA_DISPLAY="(none)"

  echo "llama VLM server started. PID=$PID"
  echo "mode   : vlm:$MODEL_ARG"
  echo "backend: $BACKEND_MODE"
  echo "binary : $LLAMA_BIN"
  echo "model  : $MODEL"
  echo "mmproj : $MMPROJ"
  echo "ctx    : $CTX"
  echo "extra  : $EXTRA_DISPLAY"
  echo "ngl    : $NGL_DISPLAY"
  echo "jinja  : $JINJA_DISPLAY"
  echo "port   : $PORT"
  echo "log    : $LOG_FILE"
  echo
  echo "Check audio/mmproj log:"
  echo "  grep -iE \"has audio encoder|has vision encoder|loaded multimodal|audio input|mmproj|projector|gemma4a|gemma4v|audio_sample_rate|clip_ctx|OpenCL|CPU backend\" \"$LOG_FILE\""
  echo
  echo "Check timing after a request:"
  echo "  grep -iE \"image|slice|encode|prompt eval|eval time|total|tokens|slot|prompt\" \"$LOG_FILE\" | tail -n 120"
else
  echo "Failed to start llama VLM server."
  echo "----- last 80 log lines -----"
  [ -f "$LOG_FILE" ] && tail -n 80 "$LOG_FILE"
  rm -f "$PID_FILE" "$MODE_FILE"
  exit 1
fi
