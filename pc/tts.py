"""Text-to-speech using Piper (offline)."""

from __future__ import annotations

import logging
import struct
from pathlib import Path
from typing import Generator, List

log = logging.getLogger(__name__)

CHUNK_BYTES = 512
SAMPLE_RATE = 16000


def _piper_paths() -> tuple[Path, Path]:
    base = Path(__file__).parent / "models"
    onnx = base / "en_US-amy-medium.onnx"
    json = base / "en_US-amy-medium.onnx.json"
    return onnx, json


def synthesize_stream(text: str) -> Generator[bytes, None, None]:
    """Yield 512-byte PCM chunks at 16 kHz mono int16.

    Piper'in amy-medium model'i native 22050 Hz uretiyor. ESP32 I2S TX
    ise 16000 Hz'le surulduktugu icin once 22050 -> 16000 resample yapip
    sonra chunk'lara boluyoruz. Yoksa ses kesik kesik ve hizli duyuluyor.
    """
    onnx, cfg = _piper_paths()
    if not onnx.exists():
        log.warning("Piper model not found at %s — using fallback tone", onnx)
        yield from _fallback_beep(text)
        return

    from piper import PiperVoice
    import numpy as np

    voice = PiperVoice.load(str(onnx), config_path=str(cfg))
    raw_parts: List[bytes] = []
    piper_sr: int | None = None

    for chunk in voice.synthesize(text):
        if piper_sr is None:
            piper_sr = getattr(chunk, "sample_rate", None)
        if hasattr(chunk, "audio_int16_bytes"):
            raw_parts.append(chunk.audio_int16_bytes)
        elif isinstance(chunk, (bytes, bytearray)):
            raw_parts.append(bytes(chunk))
        else:
            raw_parts.append(bytes(chunk))

    raw = b"".join(raw_parts)
    if not raw:
        yield from _fallback_beep(text)
        return

    # Resample Piper native rate (typ. 22050) -> ESP32 I2S rate (16000)
    if piper_sr and piper_sr != SAMPLE_RATE:
        log.info("Resampling Piper %d Hz -> %d Hz (%d bytes input)",
                 piper_sr, SAMPLE_RATE, len(raw))
        samples = np.frombuffer(raw, dtype=np.int16)
        target_len = int(len(samples) * SAMPLE_RATE / piper_sr)
        x_old = np.arange(len(samples))
        x_new = np.linspace(0, len(samples) - 1, target_len)
        samples_new = np.interp(x_new, x_old, samples).astype(np.int16)
        raw = samples_new.tobytes()
        log.info("Resampled to %d bytes (~%.1f s of audio)",
                 len(raw), len(raw) / (SAMPLE_RATE * 2))

    for i in range(0, len(raw), CHUNK_BYTES):
        yield raw[i : i + CHUNK_BYTES]


def _fallback_beep(text: str) -> Generator[bytes, None, None]:
    """Simple tone if Piper model missing — allows testing without TTS model."""
    import math

    duration = min(3.0, 0.05 * len(text))
    n = int(SAMPLE_RATE * duration)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        v = int(8000 * math.sin(2 * math.pi * 440 * t))
        samples.append(v)
    raw = struct.pack(f"<{len(samples)}h", *samples)
    for i in range(0, len(raw), CHUNK_BYTES):
        yield raw[i : i + CHUNK_BYTES]
