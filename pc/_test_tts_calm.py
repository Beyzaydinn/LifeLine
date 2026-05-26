"""Smoke test for the slow + pause TTS change.

Synthesizes a fixed multi-sentence text via the updated synthesize_stream,
joins all PCM bytes, prints duration sanity check, and writes a WAV so the
user can listen.

This file is throwaway -- not imported by gui.py/main.py and safe to delete.
"""

from __future__ import annotations

import logging
import time
import wave
from pathlib import Path

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("test_tts_calm")

from tts import LENGTH_SCALE, SAMPLE_RATE, SENTENCE_SILENCE_S, synthesize_stream

TEXT = (
    "First sentence here. "
    "This is the second sentence. "
    "And this is the third one to test sentence boundaries."
)
SENTENCES = 3  # if Piper agrees with our punctuation split

out_wav = Path(__file__).parent / "tts_calm_test.wav"

log.info("Synthesizing test text (%d chars)...", len(TEXT))
log.info("  LENGTH_SCALE=%.2f  SENTENCE_SILENCE_S=%.2f", LENGTH_SCALE, SENTENCE_SILENCE_S)

t0 = time.time()
pcm = b"".join(synthesize_stream(TEXT))
elapsed = time.time() - t0

duration_s = len(pcm) / (SAMPLE_RATE * 2)
expected_speech_s = duration_s - (SENTENCES - 1) * SENTENCE_SILENCE_S
implied_speech_at_1x = expected_speech_s / LENGTH_SCALE

log.info("PCM total: %d bytes  (%.2f s @ %d Hz mono int16)", len(pcm), duration_s, SAMPLE_RATE)
log.info("  - silence contribution: ~%.2f s (%d gaps x %.2f s)",
         (SENTENCES - 1) * SENTENCE_SILENCE_S, SENTENCES - 1, SENTENCE_SILENCE_S)
log.info("  - speech contribution:  ~%.2f s", expected_speech_s)
log.info("  - implied 1.0x baseline: ~%.2f s (sanity: should be 4-6 s for 3 short sentences)",
         implied_speech_at_1x)
log.info("Synthesis wall time: %.2f s", elapsed)

with wave.open(str(out_wav), "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(SAMPLE_RATE)
    w.writeframes(pcm)
log.info("Wrote %s -- play this to confirm pacing.", out_wav)
