# Third-party licenses

This directory contains license information for third-party libraries used by StackChan Minimal.

このディレクトリには、StackChan Minimal が利用するサードパーティライブラリのライセンス情報を配置しています。

## Included license texts

| Component | License | Purpose | Source |
|---|---|---|---|
| MAX3010x Sensor Library | MIT | MAX30100 optical pulse sensor driver | https://github.com/devxplained/MAX3010x-Sensor-Library |
| M5Stack-Avatar | See `lib/M5Stack-Avatar/LICENSE.txt` | Avatar face rendering | Bundled local library |

## Optional external tools not included in this repository

StackChan Minimal can be configured to connect to external TTS servers such as piper-plus or a user-provided VOICEVOX-compatible adapter.

VOICEVOX, VOICEVOX Engine, VOICEVOX Core, voice models, voice libraries, and VOICEVOX installers are not included in this repository.

If you use VOICEVOX-generated audio, please install VOICEVOX separately from its official distribution source and follow the VOICEVOX software terms and each voice library's terms.

Credit examples:

- VOICEVOX:ずんだもん
- VOICEVOX:四国めたん

StackChan Minimal is not affiliated with or endorsed by VOICEVOX or the rights holders of the voice libraries.

## Notes

StackChan Minimal itself is licensed under the Apache License 2.0.

Some libraries used by PlatformIO may be downloaded during build time according to `platformio.ini`.
Please refer to each library's upstream repository for the latest license information.

The license text for the MAX3010x Sensor Library is included in this directory as:

- `MAX3010x-Sensor-Library-LICENSE.txt`

## 日本語メモ

StackChan Minimal 本体は Apache License 2.0 で公開しています。

このディレクトリには、本体とは別ライセンスで提供されるサードパーティライブラリのライセンス文を配置します。

MAX30100 センサードライバとして使用している MAX3010x Sensor Library は MIT License です。
ライセンス本文は `MAX3010x-Sensor-Library-LICENSE.txt` に収録しています。

VOICEVOX、VOICEVOX Engine、VOICEVOX Core、音声モデル、音声ライブラリ、VOICEVOXインストーラーは、このリポジトリには含めていません。
VOICEVOX音声を利用する場合は、VOICEVOXソフトウェア利用規約および各音声ライブラリの利用規約を確認してください。
