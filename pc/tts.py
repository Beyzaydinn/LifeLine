"""Text-to-speech using Piper (offline)."""

from __future__ import annotations

import logging
import struct
from pathlib import Path
from typing import Generator, List

log = logging.getLogger(__name__)

CHUNK_BYTES = 512
SAMPLE_RATE = 16000

# Konusma tempo ayarlari -- ayrintili tasarim:
# docs/superpowers/specs/2026-05-26-tts-calm-pace-design.md
# Default Piper (length_scale=1.0) ilk-yardim asistani icin fazla hizli
# duyuluyordu; bu iki sabit yavaslatmayi ve cumleler arasi nefesi kontrol
# eder. Iki dugme bagimsiz: birini buyutmek otekini etkilemez.
LENGTH_SCALE = 1.25         # Piper SynthesisConfig: >1.0 -> daha yavas, pitch korunur
SENTENCE_SILENCE_S = 0.45   # Cumleler arasi sessizlik (Piper 1.2+ kendi parametresini kaldirdi)


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

    Tempo: SynthesisConfig(length_scale=LENGTH_SCALE) ile Piper yavaslatilir.
    Cumleler arasi nefes: Piper "cumle basina bir chunk" garanti ediyor;
    ardisik chunk'lar arasina SENTENCE_SILENCE_S kadar sifir-byte ekleriz
    (resampler bu sessizligi de 22050 -> 16000'e gecirir, sure korunur).
    """
    onnx, cfg = _piper_paths()
    if not onnx.exists():
        log.warning("Piper model not found at %s — using fallback tone", onnx)
        yield from _fallback_beep(text)
        return

    from piper import PiperVoice, SynthesisConfig
    import numpy as np

    voice = PiperVoice.load(str(onnx), config_path=str(cfg))
    syn_cfg = SynthesisConfig(length_scale=LENGTH_SCALE)
    raw_parts: List[bytes] = []
    piper_sr: int | None = None

    for i, chunk in enumerate(voice.synthesize(text, syn_config=syn_cfg)):
        if piper_sr is None:
            piper_sr = getattr(chunk, "sample_rate", None)
        # Cumleler arasi sessizlik: ilk chunk haric her chunk'tan once
        # SENTENCE_SILENCE_S sn'lik sifir byte ekle. Piper'in native sample
        # rate'inde uretilir; resample sonrasi gercek calma suresi korunur.
        if i > 0 and piper_sr is not None:
            silence_samples = int(SENTENCE_SILENCE_S * piper_sr)
            raw_parts.append(b"\x00" * (silence_samples * 2))  # int16 -> 2 byte
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
