#!/data/data/com.termux/files/usr/bin/bash

PID_FILE="$HOME/AI-server-run/piper-server.pid"
WRAPPER_PID_FILE="$HOME/AI-server-run/piper-wrapper.pid"
PORT="5000"

find_piper_uvicorn_pids() {
  for d in /proc/[0-9]*; do
    [ -r "$d/cmdline" ] || continue
    CMDLINE="$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null)"
    case "$CMDLINE" in
      *uvicorn*tts_server:app*"--port $PORT"*)
        case "$CMDLINE" in
          *proot-distro* ) ;;
          * ) echo "${d##*/}" ;;
        esac
        ;;
    esac
  done
}

find_piper_wrapper_pids() {
  for d in /proc/[0-9]*; do
    [ -r "$d/cmdline" ] || continue
    CMDLINE="$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null)"
    case "$CMDLINE" in
      *proot-distro*login*ubuntu*uvicorn*tts_server:app*"--port $PORT"*)
        echo "${d##*/}"
        ;;
    esac
  done
}

UVICORN_PIDS=""
WRAPPER_PIDS=""

if [ -f "$PID_FILE" ]; then
  PID="$(cat "$PID_FILE" 2>/dev/null)"
  if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
    UVICORN_PIDS="$PID"
  else
    rm -f "$PID_FILE"
  fi
fi

if [ -z "$UVICORN_PIDS" ]; then
  UVICORN_PIDS="$(find_piper_uvicorn_pids | tr '\n' ' ' | sed 's/[[:space:]]\+$//')"
fi

if [ -f "$WRAPPER_PID_FILE" ]; then
  WPID="$(cat "$WRAPPER_PID_FILE" 2>/dev/null)"
  if [ -n "$WPID" ] && kill -0 "$WPID" 2>/dev/null; then
    WRAPPER_PIDS="$WPID"
  else
    rm -f "$WRAPPER_PID_FILE"
  fi
fi

if [ -z "$WRAPPER_PIDS" ]; then
  WRAPPER_PIDS="$(find_piper_wrapper_pids | tr '\n' ' ' | sed 's/[[:space:]]\+$//')"
fi

if [ -z "$UVICORN_PIDS" ] && [ -z "$WRAPPER_PIDS" ]; then
  echo "No running piper server process found."
  termux-wake-unlock 2>/dev/null
  exit 0
fi

echo "Stopping piper server..."

if [ -n "$UVICORN_PIDS" ]; then
  for PID in $UVICORN_PIDS; do
    echo "  send SIGINT to uvicorn PID=$PID"
    kill -INT "$PID" 2>/dev/null
  done
fi

sleep 2

REMAINING_UVICORN=""
for PID in $UVICORN_PIDS; do
  if kill -0 "$PID" 2>/dev/null; then
    REMAINING_UVICORN="$REMAINING_UVICORN $PID"
  fi
done
REMAINING_UVICORN="$(echo "$REMAINING_UVICORN" | sed 's/^ *//')"

if [ -n "$REMAINING_UVICORN" ]; then
  echo "  still alive, send SIGTERM to:$REMAINING_UVICORN"
  for PID in $REMAINING_UVICORN; do
    kill -TERM "$PID" 2>/dev/null
  done
  sleep 2
fi

for PID in $WRAPPER_PIDS; do
  if kill -0 "$PID" 2>/dev/null; then
    echo "  send SIGTERM to wrapper PID=$PID"
    kill -TERM "$PID" 2>/dev/null
  fi
done

sleep 1

FINAL_REMAINING=""
for PID in $UVICORN_PIDS $WRAPPER_PIDS; do
  [ -n "$PID" ] || continue
  if kill -0 "$PID" 2>/dev/null; then
    FINAL_REMAINING="$FINAL_REMAINING $PID"
  fi
done
FINAL_REMAINING="$(echo "$FINAL_REMAINING" | sed 's/^ *//')"

if [ -n "$FINAL_REMAINING" ]; then
  echo "  still alive, send SIGKILL to:$FINAL_REMAINING"
  for PID in $FINAL_REMAINING; do
    kill -9 "$PID" 2>/dev/null
  done
  sleep 1
fi

rm -f "$PID_FILE" "$WRAPPER_PID_FILE"
termux-wake-unlock 2>/dev/null

echo "piper server stopped."
