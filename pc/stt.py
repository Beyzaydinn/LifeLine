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

        # CPU'da calistir (int8 quantization).
        # Sebep: Windows'ta CUDA Toolkit (cublas64_12.dll) yuklu degil. Onceki
        # try/except mantigi yetmedi cunku model load OK donuyor ama transcribe
        # sirasinda CUDA library yuklenirken patliyor.
        # CPU + int8: 5 sn ses ~1-2 sn'de transkripte edilir, demo icin yeterli.
        # Ollama Llama 3.2 zaten kendi CUDA runtime'ini bundle ediyor (GPU'da)
        # yani Whisper CPU + Llama GPU paralel calisir, VRAM cakismasi olmaz.
        log.info("Loading faster-whisper small.en (CPU, int8)...")
        _model = WhisperModel("small.en", device="cpu", compute_type="int8")
    return _model


def pcm_to_wav(pcm: bytes, sample_rate: int = 16000) -> Path:
    # Debug icin pc/last_recording.wav olarak kaydediyoruz. Boylece kullanici
    # Windows Media Player'da acip ham mikrofon kaydini dinleyip Whisper'in
    # neden yanlis tanidigini anlayabilir (gurultu mu, ses dusuk mu, vs).
    path = Path(__file__).parent / "last_recording.wav"
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm)
    log.info("Recording saved to %s for inspection", path)
    return path


def transcribe(pcm: bytes, sample_rate: int = 16000) -> str:
    if len(pcm) < 3200:
        log.warning("Very short audio (%d bytes)", len(pcm))
        return ""

    wav_path = pcm_to_wav(pcm, sample_rate)
    model = _get_model()
    # vad_filter=True bazen ses kalitesi dusukken tum konusmayi sessizlik
    # sanip atiyor. Kapali kalsin; ham sesi Whisper kendi degerlendirsin.
    segments, _info = model.transcribe(
        str(wav_path),
        language="en",
        beam_size=1,
        vad_filter=False,
    )
    text = " ".join(s.text.strip() for s in segments).strip()
    log.info("Transcript: %s", text)
    return text
