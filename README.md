# StackChan Minimal

StackChan Minimal is a small AI companion robot project for M5Stack AtomS3R.

It connects an ESP32-based body to local AI services such as speech recognition, local LLMs, and text-to-speech servers.

This project is intended for demonstration, learning, and Maker activities.

## Features

- Wi-Fi configuration portal
- Local speech recognition via whisper.cpp
- Local LLM chat via OpenAI-compatible APIs
  - llama.cpp
  - LM Studio
  - Ollama
- Text-to-speech via an external TTS server
  - piper-plus
  - Optional: VOICEVOX-compatible adapter
- MAX30100 heart-rate / SpO2 reference value demo
- Optional servo motion

## Hardware

- M5Stack AtomS3R
- Atomic Voice Base / compatible audio base
- MAX30100 sensor module
- Optional servo setup

## AI server tools

This firmware does not include AI models, AI server binaries, TTS engines, voice models, or voice libraries.

You need to run compatible local services separately:

- whisper.cpp for speech-to-text
- llama.cpp, LM Studio, or Ollama for local LLM chat
- piper-plus for text-to-speech
- Optional: VOICEVOX Engine + VOICEVOX-compatible adapter for text-to-speech

## Optional: VOICEVOX TTS

StackChan Minimal can be configured to connect to an external VOICEVOX-compatible TTS adapter.

This repository does not include VOICEVOX, VOICEVOX Engine, VOICEVOX Core, voice models, voice libraries, or VOICEVOX installers.

When using VOICEVOX audio, please install VOICEVOX separately from its official distribution source and follow the VOICEVOX software terms and each voice library's terms.

Credit examples:

- VOICEVOX:ずんだもん
- VOICEVOX:四国めたん

StackChan Minimal is not affiliated with or endorsed by VOICEVOX or the rights holders of the voice libraries.

## Sensors

The current public version includes MAX30100 heart-rate / SpO2 reference value support.

ENV-Pro / BME688 environmental sensor support is not included in this public release.

## HR / SpO2 reference values

StackChan Minimal can read heart-rate and SpO2-like reference values from a MAX30100 sensor module.

These values are for demonstration and educational purposes only.

This project is not a medical device and must not be used for diagnosis, treatment, health monitoring, or healthcare decisions.

The SpO2 value is an uncalibrated reference value based on optical sensor readings. It may vary depending on finger placement, ambient light, sensor module differences, and motion.

## 日本語での注意

心拍数およびSpO2参考値は、展示・デモ・学習用途の目安です。

本プロジェクトは医療機器ではありません。診断、治療、健康管理、受診判断には使用できません。

SpO2参考値は未校正の光学センサー値から算出しており、指の置き方、周囲光、センサーモジュールの個体差、動きによって大きく変動することがあります。

現在の公開版には、ENV-Pro / BME688 環境センサー機能は含めていません。

## 任意機能: VOICEVOX TTS

StackChan Minimal は、外部TTSサーバーとして VOICEVOX 互換Adapterへ接続することができます。

本リポジトリには、VOICEVOX、VOICEVOX Engine、VOICEVOX Core、音声モデル、音声ライブラリ、VOICEVOXインストーラーは含まれていません。

VOICEVOX音声を利用する場合は、ユーザー自身が公式配布元からVOICEVOXを別途インストールし、VOICEVOXソフトウェア利用規約および各音声ライブラリの利用規約に従ってください。

クレジット表記例:

- VOICEVOX:ずんだもん
- VOICEVOX:四国めたん

StackChan Minimal は、VOICEVOXおよび各音声ライブラリの権利者による公式製品・公認製品ではありません。

## License

This project is licensed under the Apache License 2.0.

Third-party library licenses are listed in `third_party_licenses/`.
