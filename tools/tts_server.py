from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import Response, FileResponse, StreamingResponse
from pydantic import BaseModel

import hashlib
import json
import os
import pathlib
import platform
import shutil
import struct
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any, Iterator, Optional

# ============================================================
# StackChan Minimal 用 統合 TTS Server
#
# 対応:
#   - Piper-plus: つくよみちゃん-日本語
#   - VOICEVOX: ずんだもん-日本語 / 四国めたん-日本語
#
# 対応想定:
#   - Windows
#   - macOS
#   - Linux
#   - Android Termux + Ubuntu(proot)
#
# ESP32 (StackChan Minimal) 互換 endpoints:
#   GET  /tts.wav?text=...&length_scale=1.0&language=ja&character=ja-01
#   GET  /tts_stream.wav?text=...&length_scale=1.0&language=ja&character=ja-01
#   GET  /tts_live.wav?text=...&length_scale=1.0&language=ja&character=ja-01
#   POST /tts {"text":"...", "length_scale":1.0, "language":"ja", "character":"ja-01"}
#
# character の意味:
#   - ja-01 / 01 / tsukuyomi : Piper-plus つくよみちゃん
#   - ja-02 / 02 / zundamon  : VOICEVOX ずんだもん
#   - ja-03 / 03 / metan     : VOICEVOX 四国めたん
#
# 注意:
#   - Piper-plus は PIPER_EXE / PIPER_MODEL / PIPER_CONFIG / PIPER_CACHE_DIR を
#     環境変数で上書きできます。
#   - VOICEVOX は VOICEVOX_ENGINE_URL で接続先を指定できます。
#     標準は http://127.0.0.1:50021 です。
#   - VOICEVOXの speaker ID は環境変数で上書きできます。
#     VOICEVOX_SPEAKER_ZUNDAMON=3
#     VOICEVOX_SPEAKER_METAN=2
#   - /tts_live.wav は Piper-plus では raw PCM をWAV化して逐次返します。
#     VOICEVOXでは合成済みWAVを返します。
# ============================================================

IS_WINDOWS = platform.system().lower().startswith("win")

# ---- Piper-plus デフォルト値: tts_server.py のあるフォルダを基準にする ----
#
# 基本方針:
#   tts_server.py が置かれているフォルダを BASE_DIR とし、
#   その下にある bin / models-tsukuyomi-wavlm / cache を使います。
#
# Windows例:
#   C:\App\piper-plus-bin
#   ├─ tts_server.py
#   ├─ bin\piper.exe
#   ├─ models-tsukuyomi-wavlm\config.json
#   ├─ models-tsukuyomi-wavlm\tsukuyomi-wavlm-300epoch.onnx
#   └─ cache
#
# Linux / Android Termux例:
#   ~/piper-plus-bin
#   ├─ tts_server.py
#   ├─ bin/piper
#   ├─ models-tsukuyomi-wavlm/config.json
#   ├─ models-tsukuyomi-wavlm/tsukuyomi-wavlm-300epoch.onnx
#   └─ cache
#
# 必要な場合は、従来どおり以下の環境変数で上書きできます。
#   PIPER_EXE / PIPER_MODEL / PIPER_CONFIG / PIPER_CACHE_DIR

BASE_DIR = pathlib.Path(__file__).resolve().parent

if IS_WINDOWS:
    DEFAULT_PIPER_EXE = str(BASE_DIR / "bin" / "piper.exe")
else:
    DEFAULT_PIPER_EXE = str(BASE_DIR / "bin" / "piper")

DEFAULT_PIPER_MODEL = str(BASE_DIR / "models-tsukuyomi-wavlm" / "tsukuyomi-wavlm-300epoch.onnx")
DEFAULT_PIPER_CONFIG = str(BASE_DIR / "models-tsukuyomi-wavlm" / "config.json")
DEFAULT_CACHE_DIR = str(BASE_DIR / "cache")

PIPER_EXE = os.environ.get("PIPER_EXE", DEFAULT_PIPER_EXE)
PIPER_MODEL = os.environ.get("PIPER_MODEL", DEFAULT_PIPER_MODEL)
PIPER_CONFIG = os.environ.get("PIPER_CONFIG", DEFAULT_PIPER_CONFIG)
CACHE_DIR = pathlib.Path(os.environ.get("PIPER_CACHE_DIR", DEFAULT_CACHE_DIR))
CACHE_DIR.mkdir(parents=True, exist_ok=True)

# true のとき `-q` を付ける。piper-plusで不要な場合は PIPER_QUIET=0 にする。
PIPER_QUIET = os.environ.get("PIPER_QUIET", "1").strip().lower() not in ("0", "false", "no", "off")

# 一般向けは False。dev版の --output-raw --streaming を試す場合のみ 1 にする。
USE_PIPER_DEV_STREAMING = os.environ.get("PIPER_USE_DEV_STREAMING", "0").strip().lower() in ("1", "true", "yes", "on")

# ---- VOICEVOX 設定 ----
VOICEVOX_ENGINE_URL = os.environ.get("VOICEVOX_ENGINE_URL", "http://127.0.0.1:50021").rstrip("/")
VOICEVOX_TIMEOUT_SEC = float(os.environ.get("VOICEVOX_TIMEOUT_SEC", "30"))
VOICEVOX_SPEAKER_ZUNDAMON = int(os.environ.get("VOICEVOX_SPEAKER_ZUNDAMON", "3"))
VOICEVOX_SPEAKER_METAN = int(os.environ.get("VOICEVOX_SPEAKER_METAN", "2"))

LENGTH_SCALE_MIN = 0.5
LENGTH_SCALE_MAX = 2.0
LENGTH_SCALE_DEFAULT = 1.0

LANGUAGE_ALIASES = {
    "ja": "ja",
    "ja-jp": "ja",
    "jp": "ja",
    "en": "en",
    "en-us": "en",
    "en-gb": "en",
    "zh": "zh",
    "zh-cn": "zh",
    "zh-tw": "zh",
}
LANGUAGE_DEFAULT = "ja"

# ESP32側で character が未指定だった場合は、従来互換として Piper-plus つくよみちゃんにする。
CHARACTER_DEFAULT = os.environ.get("TTS_DEFAULT_CHARACTER", "ja-01")

CHARACTER_BACKENDS = {
    "ja-01": "piper",
    "ja-02": "voicevox",
    "ja-03": "voicevox",
}

CHARACTER_LABELS = {
    "ja-01": "つくよみちゃん-日本語",
    "ja-02": "VOICEVOXずんだもん-日本語",
    "ja-03": "VOICEVOX四国めたん-日本語",
}

VOICEVOX_SPEAKERS = {
    "ja-02": VOICEVOX_SPEAKER_ZUNDAMON,
    "ja-03": VOICEVOX_SPEAKER_METAN,
}

CHARACTER_ALIASES = {
    "": CHARACTER_DEFAULT,
    "default": CHARACTER_DEFAULT,
    "01": "ja-01",
    "ja01": "ja-01",
    "ja-01": "ja-01",
    "tsukuyomi": "ja-01",
    "tsukuyomi-chan": "ja-01",
    "piper": "ja-01",
    "02": "ja-02",
    "ja02": "ja-02",
    "ja-02": "ja-02",
    "zundamon": "ja-02",
    "ずんだもん": "ja-02",
    "03": "ja-03",
    "ja03": "ja-03",
    "ja-03": "ja-03",
    "metan": "ja-03",
    "shikoku-metan": "ja-03",
    "四国めたん": "ja-03",
}

# live 経路用パラメータ
LIVE_CHUNK_BYTES = 4096

# Piper-plus / Tsukuyomi の想定フォーマット
WAV_SAMPLE_RATE = 22050
WAV_BITS_PER_SAMPLE = 16
WAV_CHANNELS = 1

app = FastAPI(
    title="Unified TTS Server for StackChan Minimal",
    version="1.5.0-unified-piper-plus-voicevox",
)


def _log(msg: str) -> None:
    print(f"[tts_server] {time.strftime('%Y-%m-%d %H:%M:%S')} {msg}", flush=True)


def _clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def _normalize_language(language: Optional[str]) -> str:
    if not language:
        return LANGUAGE_DEFAULT
    key = language.strip().lower()
    if not key:
        return LANGUAGE_DEFAULT
    return LANGUAGE_ALIASES.get(key, key)


def _normalize_character(character: Optional[str]) -> str:
    if character is None:
        key = ""
    else:
        key = str(character).strip().lower()

    normalized = CHARACTER_ALIASES.get(key, key)
    if normalized not in CHARACTER_BACKENDS:
        # 未知のcharacterで500にせず、従来互換のつくよみちゃんへフォールバックする。
        _log(f"unknown character='{character}', fallback to {CHARACTER_DEFAULT}")
        return CHARACTER_DEFAULT

    return normalized


def _backend_for_character(character: Optional[str]) -> str:
    return CHARACTER_BACKENDS[_normalize_character(character)]


def _looks_like_path(value: str) -> bool:
    if not value:
        return False
    lower = value.lower()
    return (
        "/" in value
        or "\\" in value
        or lower.endswith((".exe", ".onnx", ".json", ".bin"))
    )


def _check_piper_paths() -> None:
    if _looks_like_path(PIPER_EXE):
        if not pathlib.Path(PIPER_EXE).exists():
            raise HTTPException(status_code=500, detail=f"piper executable not found: {PIPER_EXE}")
    else:
        if shutil.which(PIPER_EXE) is None:
            raise HTTPException(status_code=500, detail=f"piper executable not found in PATH: {PIPER_EXE}")

    # PIPER_MODEL は、Windowsでは .onnx パス、Termux/Linuxでは "tsukuyomi" のようなモデル名にもできる。
    if _looks_like_path(PIPER_MODEL) and not pathlib.Path(PIPER_MODEL).exists():
        raise HTTPException(status_code=500, detail=f"model not found: {PIPER_MODEL}")

    if PIPER_CONFIG:
        if _looks_like_path(PIPER_CONFIG) and not pathlib.Path(PIPER_CONFIG).exists():
            raise HTTPException(status_code=500, detail=f"config not found: {PIPER_CONFIG}")


def _cache_key(
    text: str,
    length_scale: float,
    language: Optional[str],
    character: Optional[str] = None,
    extra: str = "",
) -> str:
    lang = _normalize_language(language)
    char = _normalize_character(character)
    src = f"{text}\n{length_scale:.4f}\n{lang}\n{char}\n{extra}"
    return hashlib.sha256(src.encode("utf-8")).hexdigest()


def _build_piper_cmd(length_scale: float, language: Optional[str]) -> list[str]:
    ls = _clamp(length_scale, LENGTH_SCALE_MIN, LENGTH_SCALE_MAX)
    lang = _normalize_language(language)

    cmd = [PIPER_EXE]
    if PIPER_QUIET:
        cmd += ["-q"]

    cmd += [
        "--model", PIPER_MODEL,
    ]

    if PIPER_CONFIG:
        cmd += ["--config", PIPER_CONFIG]

    cmd += [
        "--length_scale", f"{ls:.4f}",
        "--language", lang,
    ]

    return cmd


def _synthesize_piper(
    text: str,
    length_scale: float,
    language: Optional[str] = None,
    character: Optional[str] = None,
) -> pathlib.Path:
    _check_piper_paths()

    cmd = _build_piper_cmd(length_scale, language)

    ls = _clamp(length_scale, LENGTH_SCALE_MIN, LENGTH_SCALE_MAX)
    lang = _normalize_language(language)
    char = _normalize_character(character)
    out_path = CACHE_DIR / f"{_cache_key(text, ls, lang, char, extra='piper')}.wav"

    if out_path.exists() and out_path.stat().st_size > 44:
        return out_path

    cmd = cmd + ["--output_file", str(out_path)]

    try:
        result = subprocess.run(
            cmd,
            input=text.encode("utf-8"),
            capture_output=True,
            timeout=60,
            check=False,
        )
    except FileNotFoundError:
        raise HTTPException(status_code=500, detail=f"piper executable not found: {PIPER_EXE}")
    except subprocess.TimeoutExpired:
        raise HTTPException(status_code=504, detail="piper timed out")

    if result.returncode != 0:
        stderr = result.stderr.decode("utf-8", errors="replace")
        stdout = result.stdout.decode("utf-8", errors="replace")
        raise HTTPException(
            status_code=500,
            detail=f"piper error\nstdout={stdout}\nstderr={stderr}",
        )

    if not out_path.exists() or out_path.stat().st_size <= 44:
        raise HTTPException(status_code=500, detail="WAV output is empty")

    return out_path


def _voicevox_speed_scale_from_length_scale(length_scale: float) -> float:
    # Piper-plusの length_scale は大きいほどゆっくり。
    # VOICEVOXの speedScale は大きいほど速いので、逆数で近い体感にする。
    ls = _clamp(length_scale, LENGTH_SCALE_MIN, LENGTH_SCALE_MAX)
    return _clamp(1.0 / ls, 0.5, 2.0)


def _http_post_json_or_bytes(
    url: str,
    *,
    body: Optional[bytes],
    headers: Optional[dict[str, str]] = None,
    timeout: float = VOICEVOX_TIMEOUT_SEC,
) -> tuple[bytes, str]:
    req = urllib.request.Request(
        url,
        data=body if body is not None else b"",
        headers=headers or {},
        method="POST",
    )

    try:
        with urllib.request.urlopen(req, timeout=timeout) as res:
            content_type = res.headers.get("Content-Type", "")
            return res.read(), content_type
    except urllib.error.HTTPError as e:
        detail = e.read().decode("utf-8", errors="replace")
        raise HTTPException(
            status_code=502,
            detail=f"VOICEVOX HTTP error: status={e.code}, url={url}, body={detail}",
        )
    except urllib.error.URLError as e:
        raise HTTPException(
            status_code=502,
            detail=f"VOICEVOX connection error: url={url}, error={e}",
        )


def _synthesize_voicevox_bytes(
    text: str,
    length_scale: float,
    language: Optional[str] = None,
    character: Optional[str] = None,
) -> bytes:
    lang = _normalize_language(language)
    if lang != "ja":
        raise HTTPException(status_code=400, detail=f"VOICEVOX only supports Japanese in this server: language={lang}")

    char = _normalize_character(character)
    speaker = VOICEVOX_SPEAKERS.get(char)
    if speaker is None:
        raise HTTPException(status_code=400, detail=f"character is not a VOICEVOX voice: {char}")

    query_params = urllib.parse.urlencode({"text": text, "speaker": str(speaker)})
    audio_query_url = f"{VOICEVOX_ENGINE_URL}/audio_query?{query_params}"

    query_bytes, _ = _http_post_json_or_bytes(audio_query_url, body=b"")
    try:
        audio_query = json.loads(query_bytes.decode("utf-8"))
    except json.JSONDecodeError as e:
        raise HTTPException(status_code=502, detail=f"VOICEVOX audio_query returned invalid JSON: {e}")

    audio_query["speedScale"] = _voicevox_speed_scale_from_length_scale(length_scale)
    # ESP32側でモノラルWAVとして扱いやすいよう、明示的にモノラルにする。
    audio_query["outputStereo"] = False

    synthesis_params = urllib.parse.urlencode({"speaker": str(speaker)})
    synthesis_url = f"{VOICEVOX_ENGINE_URL}/synthesis?{synthesis_params}"
    wav_bytes, content_type = _http_post_json_or_bytes(
        synthesis_url,
        body=json.dumps(audio_query, ensure_ascii=False).encode("utf-8"),
        headers={"Content-Type": "application/json"},
    )

    if len(wav_bytes) <= 44:
        raise HTTPException(
            status_code=502,
            detail=f"VOICEVOX returned empty WAV: content_type={content_type}, bytes={len(wav_bytes)}",
        )

    return wav_bytes


def _synthesize_voicevox_cached(
    text: str,
    length_scale: float,
    language: Optional[str] = None,
    character: Optional[str] = None,
) -> pathlib.Path:
    ls = _clamp(length_scale, LENGTH_SCALE_MIN, LENGTH_SCALE_MAX)
    lang = _normalize_language(language)
    char = _normalize_character(character)
    speaker = VOICEVOX_SPEAKERS.get(char, -1)
    out_path = CACHE_DIR / f"{_cache_key(text, ls, lang, char, extra=f'voicevox-speaker-{speaker}')}.wav"

    if out_path.exists() and out_path.stat().st_size > 44:
        return out_path

    wav_bytes = _synthesize_voicevox_bytes(text, ls, lang, char)
    out_path.write_bytes(wav_bytes)
    return out_path


def _synthesize_by_character(
    text: str,
    length_scale: float,
    language: Optional[str] = None,
    character: Optional[str] = None,
) -> pathlib.Path:
    char = _normalize_character(character)
    backend = CHARACTER_BACKENDS[char]

    if backend == "piper":
        return _synthesize_piper(text, length_scale, language, char)
    if backend == "voicevox":
        return _synthesize_voicevox_cached(text, length_scale, language, char)

    raise HTTPException(status_code=500, detail=f"unknown backend: {backend}")


def _wav_header_unknown_size(
    sample_rate: int = WAV_SAMPLE_RATE,
    bits_per_sample: int = WAV_BITS_PER_SAMPLE,
    channels: int = WAV_CHANNELS,
) -> bytes:
    byte_rate = sample_rate * channels * bits_per_sample // 8
    block_align = channels * bits_per_sample // 8

    # RIFFサイズとdataサイズは長さ不定のため 0xFFFFFFFF
    return b"".join([
        b"RIFF",
        struct.pack("<I", 0xFFFFFFFF),
        b"WAVE",
        b"fmt ",
        struct.pack("<I", 16),          # PCM fmt chunk size
        struct.pack("<H", 1),           # PCM format
        struct.pack("<H", channels),
        struct.pack("<I", sample_rate),
        struct.pack("<I", byte_rate),
        struct.pack("<H", block_align),
        struct.pack("<H", bits_per_sample),
        b"data",
        struct.pack("<I", 0xFFFFFFFF),
    ])


def _start_live_piper(
    text: str,
    length_scale: float,
    language: Optional[str],
) -> tuple[subprocess.Popen, bytes, int]:
    _check_piper_paths()

    cmd = _build_piper_cmd(length_scale, language) + ["--output-raw"]
    if USE_PIPER_DEV_STREAMING:
        cmd += ["--streaming"]

    t0 = time.perf_counter()

    try:
        proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )
    except FileNotFoundError:
        raise HTTPException(status_code=500, detail=f"piper executable not found: {PIPER_EXE}")

    assert proc.stdin is not None
    assert proc.stdout is not None
    assert proc.stderr is not None

    try:
        proc.stdin.write(text.encode("utf-8"))
        proc.stdin.close()
    except Exception:
        if proc.poll() is None:
            proc.kill()
        raise HTTPException(status_code=500, detail="failed to send text to piper")

    # Windowsのpipeはselect.selectで待てないため、全OSでブロッキングreadに統一する。
    # ここで最初のPCMチャンクを取得してからStreamingResponseを返すため、
    # 失敗時はHTTP 500/504として返しやすい。
    first_chunk = proc.stdout.read(LIVE_CHUNK_BYTES)
    first_wait_ms = int((time.perf_counter() - t0) * 1000)

    if not first_chunk:
        try:
            rc = proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            rc = None
            if proc.poll() is None:
                proc.kill()
        stderr = proc.stderr.read().decode("utf-8", errors="replace")
        raise HTTPException(
            status_code=500,
            detail=f"live stream produced no audio (rc={rc})\nstderr={stderr}",
        )

    return proc, first_chunk, first_wait_ms


def _iter_live_wav(proc: subprocess.Popen, first_chunk: bytes) -> Iterator[bytes]:
    assert proc.stdout is not None
    assert proc.stderr is not None

    total_bytes = 0
    chunk_count = 0
    t0 = time.perf_counter()

    try:
        yield _wav_header_unknown_size()

        total_bytes += len(first_chunk)
        chunk_count += 1
        _log(f"/tts_live.wav first chunk: bytes={len(first_chunk)}")
        yield first_chunk

        while True:
            chunk = proc.stdout.read(LIVE_CHUNK_BYTES)
            if not chunk:
                break
            total_bytes += len(chunk)
            chunk_count += 1
            yield chunk

        try:
            rc = proc.wait(timeout=60)
        except subprocess.TimeoutExpired:
            if proc.poll() is None:
                proc.kill()
            rc = -1

        stderr = proc.stderr.read().decode("utf-8", errors="replace").strip()
        elapsed_ms = int((time.perf_counter() - t0) * 1000)

        if rc != 0:
            _log(
                f"/tts_live.wav piper error: rc={rc}, chunks={chunk_count}, "
                f"bytes={total_bytes}, elapsed_ms={elapsed_ms}, stderr={stderr}"
            )
        else:
            _log(
                f"/tts_live.wav done: rc={rc}, chunks={chunk_count}, "
                f"bytes={total_bytes}, elapsed_ms={elapsed_ms}"
            )
            if stderr:
                _log(f"/tts_live.wav stderr: {stderr}")

    except GeneratorExit:
        _log("/tts_live.wav client disconnected")
        raise
    finally:
        try:
            if proc.stdout:
                proc.stdout.close()
        except Exception:
            pass
        try:
            if proc.stderr:
                proc.stderr.close()
        except Exception:
            pass
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()


def _wav_response(wav_bytes: bytes, filename: str, extra_headers: Optional[dict[str, str]] = None) -> Response:
    headers = {
        "Content-Disposition": f"inline; filename={filename}",
        "Content-Length": str(len(wav_bytes)),
        "Cache-Control": "no-store",
    }
    if extra_headers:
        headers.update(extra_headers)

    return Response(
        content=wav_bytes,
        media_type="audio/wav",
        headers=headers,
    )


@app.get("/health")
def health():
    return {
        "status": "ok",
        "platform": platform.system(),
        "piper": {
            "executable": PIPER_EXE,
            "model": PIPER_MODEL,
            "config": PIPER_CONFIG,
            "quiet": PIPER_QUIET,
            "use_dev_streaming": USE_PIPER_DEV_STREAMING,
        },
        "voicevox": {
            "engine_url": VOICEVOX_ENGINE_URL,
            "timeout_sec": VOICEVOX_TIMEOUT_SEC,
            "speakers": VOICEVOX_SPEAKERS,
        },
        "cache_dir": str(CACHE_DIR),
        "default_length_scale": LENGTH_SCALE_DEFAULT,
        "default_language": LANGUAGE_DEFAULT,
        "default_character": CHARACTER_DEFAULT,
        "characters": {
            char: {
                "label": CHARACTER_LABELS[char],
                "backend": CHARACTER_BACKENDS[char],
                "speaker": VOICEVOX_SPEAKERS.get(char),
            }
            for char in sorted(CHARACTER_BACKENDS.keys())
        },
        "supported_language_aliases": sorted(LANGUAGE_ALIASES.keys()),
        "stable_endpoints": ["/tts.wav", "/tts_stream.wav"],
        "live_endpoint": "/tts_live.wav",
        "live_backend": {
            "piper": "piper --output-raw" + (" --streaming" if USE_PIPER_DEV_STREAMING else ""),
            "voicevox": "VOICEVOX complete wav response",
        },
        "live_chunk_bytes": LIVE_CHUNK_BYTES,
        "wav_sample_rate_for_piper_live": WAV_SAMPLE_RATE,
        "wav_bits_per_sample_for_piper_live": WAV_BITS_PER_SAMPLE,
        "wav_channels_for_piper_live": WAV_CHANNELS,
    }


@app.get("/characters")
def characters():
    return {
        char: {
            "label": CHARACTER_LABELS[char],
            "backend": CHARACTER_BACKENDS[char],
            "speaker": VOICEVOX_SPEAKERS.get(char),
            "aliases": sorted([k for k, v in CHARACTER_ALIASES.items() if v == char and k]),
        }
        for char in sorted(CHARACTER_BACKENDS.keys())
    }


class TTSRequest(BaseModel):
    text: str
    length_scale: Optional[float] = LENGTH_SCALE_DEFAULT
    language: Optional[str] = LANGUAGE_DEFAULT
    character: Optional[str] = CHARACTER_DEFAULT


@app.post("/tts")
async def tts_post(req: TTSRequest):
    if not req.text.strip():
        raise HTTPException(status_code=400, detail="text is empty")

    char = _normalize_character(req.character)
    backend = CHARACTER_BACKENDS[char]

    out_path = _synthesize_by_character(
        req.text,
        req.length_scale if req.length_scale is not None else LENGTH_SCALE_DEFAULT,
        req.language,
        char,
    )
    wav_bytes = out_path.read_bytes()

    _log(f"/tts POST ok: backend={backend}, character={char}, bytes={len(wav_bytes)}")

    return _wav_response(
        wav_bytes,
        "tts.wav",
        extra_headers={
            "X-TTS-Backend": backend,
            "X-TTS-Character": char,
        },
    )


@app.get("/tts.wav")
async def tts_get(
    text: str = Query(..., description="読み上げテキスト（URLエンコード済み）"),
    length_scale: float = Query(LENGTH_SCALE_DEFAULT, description="話速 (0.5〜2.0、1.0が標準)"),
    language: Optional[str] = Query(LANGUAGE_DEFAULT, description="言語指定: ja / en / zh"),
    character: Optional[str] = Query(CHARACTER_DEFAULT, description="ja-01 / ja-02 / ja-03"),
):
    if not text.strip():
        raise HTTPException(status_code=400, detail="text is empty")

    char = _normalize_character(character)
    backend = CHARACTER_BACKENDS[char]

    out_path = _synthesize_by_character(text, length_scale, language, char)
    wav_bytes = out_path.read_bytes()

    _log(f"/tts.wav ok: backend={backend}, character={char}, bytes={len(wav_bytes)}")

    return _wav_response(
        wav_bytes,
        "tts.wav",
        extra_headers={
            "X-TTS-Backend": backend,
            "X-TTS-Character": char,
        },
    )


@app.get("/tts_stream.wav")
async def tts_stream_get(
    text: str = Query(..., description="読み上げテキスト（URLエンコード済み）"),
    length_scale: float = Query(LENGTH_SCALE_DEFAULT, description="話速 (0.5〜2.0、1.0が標準)"),
    language: Optional[str] = Query(LANGUAGE_DEFAULT, description="言語指定: ja / en / zh"),
    character: Optional[str] = Query(CHARACTER_DEFAULT, description="ja-01 / ja-02 / ja-03"),
):
    if not text.strip():
        raise HTTPException(status_code=400, detail="text is empty")

    char = _normalize_character(character)
    backend = CHARACTER_BACKENDS[char]
    out_path = _synthesize_by_character(text, length_scale, language, char)

    _log(f"/tts_stream.wav ok: backend={backend}, character={char}, file={out_path.name}")

    return FileResponse(
        path=str(out_path),
        media_type="audio/wav",
        filename="tts_stream.wav",
        headers={
            "X-TTS-Backend": backend,
            "X-TTS-Character": char,
        },
    )


@app.get("/tts_live.wav")
async def tts_live_get(
    text: str = Query(..., description="読み上げテキスト（URLエンコード済み）"),
    length_scale: float = Query(LENGTH_SCALE_DEFAULT, description="話速 (0.5〜2.0、1.0が標準)"),
    language: Optional[str] = Query(LANGUAGE_DEFAULT, description="言語指定: ja / en / zh"),
    character: Optional[str] = Query(CHARACTER_DEFAULT, description="ja-01 / ja-02 / ja-03"),
):
    if not text.strip():
        raise HTTPException(status_code=400, detail="text is empty")

    char = _normalize_character(character)
    backend = CHARACTER_BACKENDS[char]

    if backend == "voicevox":
        t0 = time.perf_counter()
        out_path = _synthesize_voicevox_cached(text, length_scale, language, char)
        wav_bytes = out_path.read_bytes()
        elapsed_ms = int((time.perf_counter() - t0) * 1000)

        _log(
            f"/tts_live.wav VOICEVOX ok: character={char}, "
            f"speaker={VOICEVOX_SPEAKERS.get(char)}, bytes={len(wav_bytes)}, elapsed_ms={elapsed_ms}"
        )

        return _wav_response(
            wav_bytes,
            "tts_live.wav",
            extra_headers={
                "X-TTS-Mode": "voicevox-complete-wav",
                "X-TTS-Backend": backend,
                "X-TTS-Character": char,
                "X-VOICEVOX-Speaker": str(VOICEVOX_SPEAKERS.get(char)),
            },
        )

    # Piper-plus / つくよみちゃんは従来の低遅延経路
    proc, first_chunk, first_wait_ms = _start_live_piper(text, length_scale, language)

    _log(
        f"/tts_live.wav Piper ok: first_wait_ms={first_wait_ms}, "
        f"first_chunk_bytes={len(first_chunk)}, language={_normalize_language(language)}, "
        f"length_scale={_clamp(length_scale, LENGTH_SCALE_MIN, LENGTH_SCALE_MAX):.4f}, "
        f"character={char}, dev_streaming={USE_PIPER_DEV_STREAMING}"
    )

    return StreamingResponse(
        _iter_live_wav(proc, first_chunk),
        media_type="audio/wav",
        headers={
            "Content-Disposition": "inline; filename=tts_live.wav",
            "Cache-Control": "no-store",
            "X-TTS-Mode": "piper-live-raw-wrapped" + ("-dev-streaming" if USE_PIPER_DEV_STREAMING else ""),
            "X-TTS-Backend": backend,
            "X-TTS-Character": char,
        },
    )
