"""Speech-to-text using faster-whisper (offline)."""

from __future__ import annotations

import logging
import os
import threading
import wave
from pathlib import Path

# Full-offline guarantee. When faster-whisper loads the model BY NAME ("small.en"),
# huggingface_hub by default makes an HTTP request to huggingface.co to verify the
# repo revision. Since the course requirement is "fully offline, no network", we
# force all HF libraries into global offline mode. These env vars must be set BEFORE
# huggingface_hub is imported; faster_whisper is only imported inside _build_model(),
# so setting them here at "import stt" time is early enough.
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["TRANSFORMERS_OFFLINE"] = "1"

log = logging.getLogger(__name__)

_model = None
_model_lock = threading.Lock()

# The Whisper model lives in the same place as Piper: pc/models/. This keeps all
# models in one local location, independent of the HF cache, fully self-contained.
_MODEL_DIR = Path(__file__).parent / "models" / "faster-whisper-small.en"


def _register_cuda_dll_dirs() -> None:
    """Windows: make the CUDA DLLs needed by the CTranslate2 GPU path loadable.

    The bin/ folders of the nvidia-*-cu12 wheels (cublas64_12.dll, ...) and the
    CTranslate2 package directory (which ships the bundled cudnn64_9.dll) are
    registered both via add_dll_directory and on PATH.

    Why PATH is REQUIRED: CTranslate2 loads cublas lazily via LoadLibrary BY NAME,
    and that call searches PATH, NOT the os.add_dll_directory user dirs. With only
    add_dll_directory we got "cublas64_12.dll is not found"; the DLL itself loads
    fine from an absolute path, so it was a search-path issue, not a missing
    dependency. nvidia.cublas is a namespace package, so its __file__ is None; we
    locate the sub-packages' bin/ dirs via __path__ instead."""
    if not hasattr(os, "add_dll_directory"):
        return
    dll_dirs: list[str] = []
    try:
        import nvidia  # namespace package of the nvidia-*-cu12 wheels
        for base in getattr(nvidia, "__path__", []):
            dll_dirs += [str(p) for p in Path(base).glob("*/bin") if p.is_dir()]
    except Exception as e:  # without the wheel the GPU path below falls back to CPU
        log.debug("nvidia CUDA wheels not found: %s", e)
    try:
        import ctranslate2  # the bundled cudnn64_9.dll lives in this directory

        dll_dirs.append(str(Path(ctranslate2.__file__).parent))
    except Exception as e:
        log.debug("ctranslate2 directory not found: %s", e)

    for d in dll_dirs:
        try:
            os.add_dll_directory(d)
        except OSError as e:
            log.debug("add_dll_directory failed (%s): %s", d, e)
    if dll_dirs:
        os.environ["PATH"] = os.pathsep.join(dll_dirs) + os.pathsep + os.environ.get("PATH", "")


def _build_model(device: str, compute_type: str):
    """Build the WhisperModel and actually exercise it with a short warm-up transcribe.

    CTranslate2 raises GPU/DLL errors not at model construction but on the FIRST
    transcribe; the warm-up makes any such error surface here instead of during a
    real recording (so _get_model can fall back to CPU when the GPU path fails).
    Bonus: it pays the first real call's latency up front."""
    import numpy as np
    from faster_whisper import WhisperModel

    # Prefer the local package; otherwise fall back to the model name (still offline).
    model_ref = str(_MODEL_DIR) if _MODEL_DIR.exists() else "small.en"
    # local_files_only=True: never reach the network, load only from local files.
    model = WhisperModel(
        model_ref, device=device, compute_type=compute_type, local_files_only=True
    )
    # 1 s of silence; consuming the generator actually runs the encoder/decoder.
    segments, _ = model.transcribe(
        np.zeros(16000, dtype=np.float32), language="en", beam_size=1, vad_filter=False
    )
    list(segments)
    return model


def _get_model():
    global _model
    if _model is not None:
        return _model
    # Double-checked lock: when the prewarm thread and the transcribe thread call
    # _get_model at the same time, the model must not load TWICE (on GPU that would
    # double the VRAM usage).
    with _model_lock:
        if _model is None:
            # Try GPU first (small.en int8_float16), fall back to CPU int8 on failure.
            # int8_float16 has a small VRAM footprint, chosen so it can coexist with
            # Llama (~2.8 GB, Ollama) on the 4 GB RTX 3050. All loading is offline.
            _register_cuda_dll_dirs()
            try:
                log.info("Loading faster-whisper (GPU: cuda, int8_float16, offline)")
                _model = _build_model("cuda", "int8_float16")
                log.info("faster-whisper ready on GPU")
            except Exception as e:
                log.warning("GPU path failed (%s); falling back to CPU int8", e)
                _model = _build_model("cpu", "int8")
                log.info("faster-whisper ready on CPU")
    return _model


def prewarm() -> None:
    """Called in a background thread at GUI startup: load and warm up the model in
    advance so the first real transcribe does not wait ~2 s for loading. On error it
    returns silently; the real transcribe call will retry via _get_model."""
    try:
        _get_model()
    except Exception as e:
        log.warning("STT prewarm failed (will retry on the real call): %s", e)


def pcm_to_wav(pcm: bytes, sample_rate: int = 16000) -> Path:
    # We save this as pc/last_recording.wav for debugging, so the user can open it in
    # Windows Media Player, listen to the raw mic recording, and understand why
    # Whisper mis-recognized it (noise? too quiet? etc.).
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
    # vad_filter=True sometimes mistakes the whole speech for silence when the audio
    # quality is low and drops it. Keep it off; let Whisper judge the raw audio itself.
    segments, _info = model.transcribe(
        str(wav_path),
        language="en",
        beam_size=1,
        vad_filter=False,
    )
    text = " ".join(s.text.strip() for s in segments).strip()
    log.info("Transcript: %s", text)
    return text
