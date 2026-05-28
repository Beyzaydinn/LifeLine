"""Speech-to-text using faster-whisper (offline)."""

from __future__ import annotations

import logging
import os
import wave
from pathlib import Path

# Tam offline garantisi. faster-whisper modeli ADIYLA ("small.en") yuklenince
# huggingface_hub varsayilan olarak repo revizyonunu dogrulamak icin
# huggingface.co'ya HTTP istegi atar. Ders gereksinimi "tamamen offline, network
# yok" oldugu icin tum HF kutuphanelerini global offline moda zorluyoruz. Bu env
# var'lar huggingface_hub import edilmeden ONCE set olmali; faster_whisper ancak
# _get_model() icinde import edildigi icin "import stt" ile burada zamaninda set olur.
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["TRANSFORMERS_OFFLINE"] = "1"

log = logging.getLogger(__name__)

_model = None

# Whisper modeli Piper ile ayni yerde tutulur: pc/models/. Boylece tum modeller
# tek yerden, HF cache konumundan bagimsiz, tamamen yerel yuklenir (self-contained).
_MODEL_DIR = Path(__file__).parent / "models" / "faster-whisper-small.en"


def _get_model():
    global _model
    if _model is None:
        from faster_whisper import WhisperModel

        # Once yerel pakete bak; yoksa model adina dus (yine offline, HF cache).
        model_ref = str(_MODEL_DIR) if _MODEL_DIR.exists() else "small.en"

        # CPU + int8 zorunlu: Windows'ta CUDA Toolkit (cublas64_12.dll) yuklu
        # degil, GPU yolu transcribe sirasinda patliyordu. CPU'da 5 sn ses
        # ~1-2 sn'de biter. Llama (Ollama) kendi CUDA runtime'iyle GPU'da kalir.
        log.info("Loading faster-whisper small.en from %s (CPU, int8, offline)", model_ref)
        # local_files_only=True: asla network'e cikma, sadece yerelden yukle.
        _model = WhisperModel(
            model_ref, device="cpu", compute_type="int8", local_files_only=True
        )
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
