"""Speech-to-text using faster-whisper (offline)."""

from __future__ import annotations

import logging
import tempfile
import wave
from pathlib import Path
from typing import Optional

log = logging.getLogger(__name__)

_model = None


def _get_model():
    global _model
    if _model is None:
        from faster_whisper import WhisperModel

        log.info("Loading faster-whisper small.en (may take a minute)...")
        try:
            _model = WhisperModel("small.en", device="cuda", compute_type="float16")
        except Exception:
            log.warning("CUDA unavailable — using CPU")
            _model = WhisperModel("small.en", device="cpu", compute_type="int8")
    return _model


def pcm_to_wav(pcm: bytes, sample_rate: int = 16000) -> Path:
    path = Path(tempfile.gettempdir()) / "first_aid_recording.wav"
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm)
    return path


def transcribe(pcm: bytes, sample_rate: int = 16000) -> str:
    if len(pcm) < 3200:
        log.warning("Very short audio (%d bytes)", len(pcm))
        return ""

    wav_path = pcm_to_wav(pcm, sample_rate)
    model = _get_model()
    segments, _info = model.transcribe(
        str(wav_path),
        language="en",
        beam_size=1,
        vad_filter=True,
    )
    text = " ".join(s.text.strip() for s in segments).strip()
    log.info("Transcript: %s", text)
    return text
