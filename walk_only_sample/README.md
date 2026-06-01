# PCA9685 Servo Sweep Test for AtomS3R

AtomS3R + PCA9685の最小テスト用ファームウェアおよびビジュアル操作ツールです。

- 起動時にPCA9685の全16チャンネルを90度へ移動します。
- 画面タップ、または本体ボタンで、全16チャンネルを `90 -> 0 -> 90 -> 180 -> 90` の順に1回動かします。
- WiFi接続後、AtomS3Rの画面に表示されたIPへブラウザでアクセスすると、CH0〜7をスライダーで指定できます。
- 対角歩行: 右前+左後 → 左前+右後 を交互に出し、接地側で軽く押す9フレームのシーケンス（`walk` / `walk_fast`）。
- ボタンA: Walk loop の開始/停止。
- AtomS3R本体のGROVE/HY2.0-4P端子を使い、`SDA=GPIO1` / `SCL=GPIO2` に固定しています。

## Robot Viewer (WebGL 3Dビューア)

`pca9685_servo_sweep_test/robot_viewer.html` をブラウザで開いて使用します。
- 接続ボタンを押すと、デバイス本体に保存されているモーション（`walk`）を自動で読み込み、タイムライン上に展開します（HTML側から歩行プリセットを毎回アップロードする送信処理は廃止されました）。
- タイムラインから各コマのミリ秒数調整や、ドラッグによる関節角度編集が可能です。
- UIは視認性の高いライトテーマになっています。

## Web Control

WiFi configuration:

```text
cp walk_only_sample/src/local_config.example.h walk_only_sample/src/local_config.h
```

Then edit `walk_only_sample/src/local_config.h` with your local SSID and password.

HTTP API (モーションは PSRAM + LittleFS `/m/{name}.json` に保存):

```text
POST /upload?name=walk          # body: motion JSON
GET  /motions                   # 一覧
GET  /motion?name=walk          # JSON 取得
GET  /play?name=walk            # 1回再生
GET  /loop/start?name=walk      # ループ再生
GET  /loop/start?name=walk_fast # 速い歩行
GET  /loop/stop                 # 停止
GET  /sit/play  GET /stand      # 既定 sit / stand モーション
GET  /sequence?name=walk        # 互換 (GET/POST)
```

モーション JSON 例:

```json
{
  "name": "my_walk",
  "loopStartFrame": 3,
  "frames": [
    { "ms": 120, "angles": [56,108,112,78,68,102,124,72] },
    { "ms": 130, "angles": [54,128,112,84,68,96,126,52] }
  ]
}
```

`angles` は CH0〜7（右前膝/股、右後膝/股、左前膝/股、左後膝/股）。初回起動で flash が空なら `walk` / `walk_fast` / `sit` / `stand` を自動生成します。

## Wiring

PCA9685 logic side:

```text
Atom G   -> PCA9685 GND
Atom 5V  -> PCA9685 VCC
Atom GROVE white / G1 -> PCA9685 SDA
Atom GROVE yellow / G2 -> PCA9685 SCL
```

Servo power:

```text
External 5V + -> PCA9685 V+
External 5V - -> PCA9685 GND
```

Atom側GND、PCA9685 GND、サーボ電源GNDは共通にしてください。

## Commands

```bash
cd /Users/ruchi/Documents/GitHub/StackChan_Minimal/pca9685_servo_sweep_test
pio run -e m5stack-atoms3
pio run -e m5stack-atoms3 -t upload --upload-port /dev/cu.usbmodem1101
pio device monitor -p /dev/cu.usbmodem1101 -b 115200
```
