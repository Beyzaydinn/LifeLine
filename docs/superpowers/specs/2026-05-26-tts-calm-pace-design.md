# TTS Calm Pace — Design

**Date:** 2026-05-26
**Component:** `pc/tts.py`
**Scope:** Make Piper TTS output ~25% slower and insert ~450 ms of silence
between sentences so the first-aid reply sounds deliberate instead of rushed.

---

## Problem

The current `synthesize_stream()` calls `voice.synthesize(text)` with no
synthesis configuration. Piper therefore runs with its default
`length_scale=1.0`, which on `en_US-amy-medium` is the model's fastest
"natural" tempo. End-to-end this produces audible output that, in the user's
words, is *"çok hızlı"* — too fast for a first-aid assistant whose listener
may be panicking.

A stale comment in `firmware/main/config.h` (the audio sample-rate block)
already promises that Piper is slowed down on the PC side via
`tts.py: length_scale`. That promise was never kept — the parameter is not
referenced anywhere in `tts.py`.

## Goals

1. The synthesized reply, after the existing 22050 → 16000 Hz resample, is
   about 25 % longer in wall-clock time than today, with the same pitch and
   timbre.
2. Between sentences there is a clearly audible (but not lingering) pause of
   roughly 450 ms.
3. No regressions: empty text, single-sentence text, and the existing
   fallback beep all keep working.
4. No new pip dependencies. No changes to the ESP32 firmware audio path,
   which already runs at 16 kHz.

## Non-Goals

- Switching to a different Piper voice model (kept on the table for later).
- Adding SSML / prosody markup.
- Time-stretching the output buffer with librosa or pyrubberband (would add
  CPU cost and a dependency; Piper's native knob does it cleaner).
- Touching the LLM prompt or the ESP I2S playback rate.

## API Reality Check

Confirmed by introspecting the installed package
(`piper-tts>=1.2.0`, file at
`pc/.venv/Lib/site-packages/piper/__init__.py`):

```text
PiperVoice.synthesize(self, text, syn_config=None, include_alignments=False)
SynthesisConfig(speaker_id, length_scale, noise_scale, noise_w_scale,
                normalize_audio=True, volume=1.0)
```

Two facts drive the design:

1. **`length_scale` is the supported knob.** It lives on `SynthesisConfig`,
   not on `synthesize()` directly. A value > 1.0 stretches phoneme
   durations, preserving pitch.
2. **`sentence_silence` does NOT exist on `SynthesisConfig` in 1.2+.** It
   was present in older Piper. However, the official docstring states
   *"Synthesize one audio chunk per sentence from text"* — so the natural
   sentence boundary is still exposed to the caller. We insert silence
   ourselves between consecutive chunks.

## Design

### Constants (top of `pc/tts.py`)

```python
LENGTH_SCALE = 1.25         # ~25% slower; preserves pitch
SENTENCE_SILENCE_S = 0.45   # silence between sentences, in seconds
```

These live as module-level constants so they are discoverable and tunable
without digging into the synthesis function. They are intentionally not
threaded through `synthesize_stream()` as kwargs — there is exactly one
caller pattern and exposing them as parameters now would be premature
abstraction.

### Synthesis call

```python
from piper import PiperVoice, SynthesisConfig

syn_cfg = SynthesisConfig(length_scale=LENGTH_SCALE)
for i, chunk in enumerate(voice.synthesize(text, syn_config=syn_cfg)):
    if piper_sr is None:
        piper_sr = getattr(chunk, "sample_rate", None)
    if i > 0 and piper_sr is not None:
        # zero-fill at Piper's native rate; resampler converts it
        # losslessly to 16 kHz silence, preserving the 0.45 s duration.
        silence_samples = int(SENTENCE_SILENCE_S * piper_sr)
        raw_parts.append(b"\x00" * (silence_samples * 2))  # int16 → 2 B/sample
    # existing audio extraction (audio_int16_bytes / bytes fallback) unchanged
    ...
```

The `i > 0` guard means silence is placed *between* sentences only, never
before the first or after the last — the latter would lengthen the perceived
playback end and the ESP `PLAYBACK_END` ack would arrive late.

### Resample interaction

Silence is appended in Piper's native frame (typically 22 050 Hz) so that
the existing `np.interp` resample step downstream treats it identically to
real audio. Because silence resamples to silence with no artefacts, the
wall-clock duration of the gap (`SENTENCE_SILENCE_S`) is preserved exactly
under the 22050 → 16000 conversion. We do not need a separate code path.

### `length_scale` is independent of silence

The two knobs intentionally do not couple. Doubling `length_scale` makes
words drawl; doubling `SENTENCE_SILENCE_S` makes pauses lengthen. Mixing
them would surprise the tuner.

### Stale comment fix in firmware

`firmware/main/config.h` says, in the audio block:

> *Pipeline hizini ayarlamak icin Piper'i PC tarafinda yavaslatiyoruz
> (tts.py: length_scale).*

After this change that statement is finally true. Update the comment to
reference both constants and to drop the "denendi ama" hedge, so the next
reader does not waste time looking for the dead promise.

## Risk & Edge Cases

| Case | Behaviour |
|---|---|
| Empty input string | `voice.synthesize("")` yields no chunks → existing fallback beep path triggers as today. |
| Single sentence | Loop runs once with `i == 0` → no silence inserted. Correct. |
| Piper raises on `syn_config` | Highly unlikely on 1.2+ but if so, fallback beep covers the user; surface via `log.warning`. |
| Long reply (many sentences) | N silence inserts of 0.45 s — for a 5-sentence reply that adds ~1.8 s. Acceptable; the user explicitly asked for this. |
| `piper_sr` not present on first chunk | Both today's code and this design guard with `getattr(... , None)`; silence insert is also gated on `piper_sr is not None`. |

## Verification

1. **Unit-ish smoke:** call `synthesize_stream("First sentence. Second.
   Third one here.")` standalone, write the joined PCM to a `.wav`, open it
   in a player. Expect: clearly slower delivery than the baseline recording
   captured during the last session, audible pauses at the periods.
2. **End-to-end:** run `gui.py`, record a question, let Llama answer with a
   multi-sentence first-aid reply, listen via the ESP speaker. Expect the
   same perceptual change as the smoke test.
3. **Length sanity:** log `len(raw)` before and after the change for the
   same input text; expect the new length ≈ `1.25 × old + N × 0.45 s ×
   16000 × 2`, where N is `sentence_count - 1`.

## Out of Scope (Backlog)

- Exposing `LENGTH_SCALE` / `SENTENCE_SILENCE_S` via `config.py` if multiple
  call sites ever need different tempos.
- Auto-tuning length_scale per text length (shorter answers slower, longer
  answers slightly faster).
- Switching voice models (e.g. `en_GB-alba-medium`, `en_US-lessac-medium`)
  to change character rather than pace.
