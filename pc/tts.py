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
    """Yield 512-byte PCM chunks at 16 kHz mono int16."""
    onnx, cfg = _piper_paths()
    if not onnx.exists():
        log.warning("Piper model not found at %s — using fallback tone", onnx)
        yield from _fallback_beep(text)
        return

    from piper import PiperVoice

    voice = PiperVoice.load(str(onnx), config_path=str(cfg))
    raw = b""
    for chunk in voice.synthesize(text):
        if hasattr(chunk, "audio_int16_bytes"):
            raw += chunk.audio_int16_bytes
        elif isinstance(chunk, (bytes, bytearray)):
            raw += bytes(chunk)
        else:
            raw += bytes(chunk)

    if not raw:
        yield from _fallback_beep(text)
        return

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
