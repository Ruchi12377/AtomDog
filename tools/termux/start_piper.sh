#!/data/data/com.termux/files/usr/bin/bash

# ============================================================
# StackChan Minimal 用 Piper-plus / 統合TTSサーバー起動スクリプト
#
# 実行環境:
#   Android Termux
#   + proot-distro Ubuntu
#
# 起動するサーバー:
#   Ubuntu proot 内の ~/piper/tts_server.py
#
# 起動コマンドの実体:
#   cd ~/piper
#   . ~/tts-venv/bin/activate
#   uvicorn tts_server:app --host 0.0.0.0 --port 5000
#
# ESP32 側からの接続先例:
#   http://<Android端末のIPアドレス>:5000/tts_live.wav
#
# 対応音声:
#   character=ja-01 : Piper-plus / つくよみちゃん-日本語
#   character=en-01 : Piper-plus / 英語
#
# 注意:
#   このAndroid構成では、標準ではVOICEVOX Engineを起動していない。
#   そのため character=ja-02 / ja-03 は、VOICEVOX未導入の場合 502 になる。
#   これは異常ではなく、VOICEVOX系音声が使えないことを示す正常な失敗。
# ============================================================

# uvicorn本体のPIDを保存するファイル。
# stop用スクリプトや二重起動防止で使う。
PID_FILE="$HOME/AI-server-run/piper-server.pid"

# proot-distroを起動している外側のラッパープロセスPIDを保存するファイル。
# uvicorn本体とは別プロセスになるため分けて管理する。
WRAPPER_PID_FILE="$HOME/AI-server-run/piper-wrapper.pid"

# サーバー起動ログの保存先。
# 起動失敗時はこのログの末尾を表示する。
LOG_FILE="$HOME/AI-server-run/piper-server.log"

# ESP32から接続するTTSサーバーのポート。
# Windows版で5001を使っていても、Android Termux版では5000を使う運用。
PORT="5000"


# ============================================================
# 実行中の uvicorn tts_server:app のPIDを探す関数
#
# proot-distro login ubuntu 経由で起動しているため、
# 外側のproot-distroプロセスと、内側のuvicornプロセスが見える。
#
# ここでは、cmdlineに
#   uvicorn
#   tts_server:app
#   --port 5000
# を含むプロセスを探す。
#
# ただし proot-distro 自体のプロセスは除外し、
# uvicorn本体のPIDだけを返す。
# ============================================================
find_piper_uvicorn_pid() {
  for d in /proc/[0-9]*; do
    [ -r "$d/cmdline" ] || continue
    CMDLINE="$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null)"
    case "$CMDLINE" in
      *uvicorn*tts_server:app*"--port $PORT"*)
        case "$CMDLINE" in
          *proot-distro* ) ;;
          * ) echo "${d##*/}"; return 0 ;;
        esac
        ;;
    esac
  done
  return 1
}


# ============================================================
# 二重起動防止
#
# 以前保存したPID_FILEがあり、そのPIDがまだ生きている場合は、
# すでにTTSサーバーが起動済みと判断して終了する。
#
# PID_FILEはあるがプロセスが存在しない場合は、
# 古いPIDファイルと判断して削除する。
# ============================================================
if [ -f "$PID_FILE" ]; then
  PID="$(cat "$PID_FILE" 2>/dev/null)"
  if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
    echo "piper server is already running. PID=$PID"
    exit 0
  else
    rm -f "$PID_FILE"
  fi
fi


# ============================================================
# 古いラッパーPIDファイルの掃除
#
# proot-distroを起動した外側のプロセスがすでに終了している場合、
# 残っている piper-wrapper.pid を削除する。
# ============================================================
if [ -f "$WRAPPER_PID_FILE" ]; then
  WPID="$(cat "$WRAPPER_PID_FILE" 2>/dev/null)"
  if [ -n "$WPID" ] && ! kill -0 "$WPID" 2>/dev/null; then
    rm -f "$WRAPPER_PID_FILE"
  fi
fi


# ============================================================
# Android端末のスリープ抑制
#
# 長時間の展示やデモ中にAndroid側の処理が止まりにくいよう、
# Termuxのwake-lockを取得する。
#
# termux-apiが未導入でも起動自体は続行したいので、
# エラーは捨てる。
# ============================================================
termux-wake-lock 2>/dev/null


# ============================================================
# Ubuntu proot内でTTSサーバーを起動
#
# 処理内容:
#   1. proot-distro login ubuntu でUbuntu環境に入る
#   2. ~/piper に移動
#   3. ~/tts-venv を有効化
#   4. uvicornで tts_server.py の app を起動
#
# nohup + バックグラウンド起動により、
# Termuxのシェルを閉じてもサーバーが残る構成。
#
# stdout / stderr は LOG_FILE に保存する。
# ============================================================
nohup proot-distro login ubuntu -- bash -lc \
"cd ~/piper && . ~/tts-venv/bin/activate && exec uvicorn tts_server:app --host 0.0.0.0 --port $PORT" \
  > "$LOG_FILE" 2>&1 < /dev/null &


# proot-distroを起動した外側のラッパープロセスPIDを保存する。
WRAPPER_PID=$!
echo "$WRAPPER_PID" > "$WRAPPER_PID_FILE"


# uvicornの起動完了を少し待つ。
sleep 3


# ============================================================
# 起動確認
#
# find_piper_uvicorn_pid でuvicorn本体のPIDを探す。
# 見つかれば起動成功としてPIDを保存し、接続情報を表示する。
#
# 見つからなければ起動失敗として、ログ末尾50行を表示する。
# ============================================================
UVICORN_PID="$(find_piper_uvicorn_pid)"
if [ -n "$UVICORN_PID" ] && kill -0 "$UVICORN_PID" 2>/dev/null; then
  echo "$UVICORN_PID" > "$PID_FILE"
  echo "piper server started. PID=$UVICORN_PID"
  echo "wrapper: $WRAPPER_PID"
  echo "port   : $PORT"
  echo "log    : $LOG_FILE"
else
  echo "Failed to detect uvicorn PID."
  echo "----- last 50 log lines -----"
  [ -f "$LOG_FILE" ] && tail -n 50 "$LOG_FILE"
  rm -f "$PID_FILE" "$WRAPPER_PID_FILE"
  exit 1
fi
