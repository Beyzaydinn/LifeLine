# Honest Heart-Rate (PPG) Measurement — Design

**Date:** 2026-05-28
**Component:** `firmware/main/sensor.c` (+ small `main.c`), `pc/serial_bridge.py`, `pc/gui.py`
**Scope:** Replace the fake modulo BPM/SpO2 with a real photoplethysmography
(PPG) pipeline computed on the ESP32 from the MAX30102. Heart rate is measured
honestly via beat detection; SpO2 is computed with the standard ratio-of-ratios
method and always presented as an uncalibrated estimate.

---

## Problem

`sensor_read_vitals()` today reads only 8 instantaneous FIFO samples in a burst
and fabricates the result:

```c
out->spo2       = 110 - 25 * ((red_avg % 10000) / (ir_avg + 1));
out->heart_rate = 60 + (ir_avg % 40);   // pure modulo, not a measurement
```

These numbers are not a measurement of anything. A real heart rate requires a
*continuous time series* of the pulsatile IR signal over several seconds so the
individual beats can be detected and timed. The sensor is already wired and
initialized in SpO2 mode (both Red and IR LEDs active), so the hardware is
capable; only the firmware logic is wrong.

There is also a Turkish DEBUG comment in `sensor_read_vitals()` (the
"tum esikleri kaldirdik" block) that violates the project's English-only rule
for artifacts; it disappears when the function is rewritten.

## Goals

1. **Honest heart rate.** BPM is derived from detected beats in the IR PPG
   waveform, accurate to within ~±5 BPM of a hand-counted pulse, and
   repeatable.
2. **Honest SpO2 estimate.** SpO2 uses the textbook ratio-of-ratios formula and
   is always labelled `(estimate)` in the GUI; no claim of clinical accuracy.
3. **No fabricated output.** When the signal is poor or no finger is present,
   the result is `valid = false` (BPM/SpO2 = 0), never a made-up number.
4. **Windowed measurement UX.** The PC "Read vitals" action runs a ~12–15 s
   measurement with live progress, then settles on a stable value.
5. **No protocol change, no risk to the working audio path.** The existing
   4-byte `MSG_SENSOR` payload (`hr_hi, hr_lo, spo2, valid`) is unchanged. No
   new PC pip dependency for the production path.

## Non-Goals

- Calibrating SpO2 against a commercial pulse oximeter (no reference device
  available; SpO2 stays an estimate). → backlog.
- Streaming raw PPG samples to the PC for a live waveform (Approach B). → backlog.
- Using the MAX30102 INT pin (GPIO 4) FIFO-almost-full interrupt; we poll. → backlog.
- A project-wide Turkish→English comment cleanup. Only comments in code we
  touch are converted here. → backlog.
- HRV or any metric beyond BPM + SpO2 estimate.

## Approach Decision

The signal processing runs **on the ESP32** (Approach A), chosen over streaming
raw samples to the PC (Approach B) or a hybrid (C). Rationale: zero risk to the
working audio/protocol pipeline, no new PC dependency, and on-device DSP is the
stronger result for an embedded IoT course. The report's PPG-waveform figure can
still be produced from a development-time log dump (see Verification), so
Approach B's main advantage (visualisation) is recovered without its plumbing.

## MAX30102 Register Reality Check

Current init values and what they decode to:

| Register | Current | Meaning |
|---|---|---|
| `SPO2_CONFIG` (0x0A) | `0x27` | ADC range 4096 nA; **sample rate 100 Hz**; pulse width 411 µs / 18-bit |
| `FIFO_CONFIG` (0x08) | `0x4F` | sample averaging **4**; **rollover OFF**; almost-full = 17 |
| `LED1_PA`/`LED2_PA` | `0x50` | ~16 mA per LED |
| `MODE_CONFIG` (0x09) | `0x03` | SpO2 mode → FIFO holds RED then IR per sample |

Effective output rate today = 100 Hz / 4 = **25 Hz**.

**Change:** set `FIFO_CONFIG = 0x30` → sample averaging **2**, **rollover ON**,
almost-full threshold 0 (unused; we poll). New effective rate = 100 Hz / 2 =
**50 Hz**, giving ~20 ms beat-timing resolution. Rollover ON matters for
continuous draining: if the sampler ever falls behind, the hardware FIFO
overwrites its oldest sample instead of freezing. `SPO2_CONFIG`, LED current and
mode are unchanged. After (re)configuring, clear `FIFO_WR_PTR`, `FIFO_RD_PTR`
and `OVF_COUNTER` to start from a clean FIFO.

The existing FIFO read order (`data[0..2]` = RED, `data[3..5]` = IR) is correct
for SpO2 mode and stays.

## Design

### Ownership and tasks

`sensor.c` fully owns the MAX30102 and its DSP. It runs its own FreeRTOS
sampler task (created in `sensor_init`) and exposes only a cheap reader:

- `sensor_init()` — configure registers (as above) and `xTaskCreate` the sampler.
- `sensor_read_vitals(out)` — copy the latest computed `{hr, spo2, valid}` from
  shared state under a `portMUX`. **No I2C here** (so it is fast and cannot race
  the sampler on the bus).

`main.c` is almost untouched:
- The `MSG_READ_SENSOR` handler still calls `send_sensor_reading()`, which now
  reads shared state (no I2C) and queues the unchanged 4-byte `MSG_SENSOR`.
- `task_sensor` still streams `MSG_SENSOR` every 200 ms **during RECORDING** so
  the LLM receives vitals — now real values. Its Turkish-free structure is kept;
  only touched comments are converted to English.

Only the sampler task performs I2C, so there is no bus contention.

### Continuous sampling

The sampler task wakes every ~80 ms and drains all available FIFO samples:

```text
n_available = (FIFO_WR_PTR - FIFO_RD_PTR) & 0x1F   // FIFO is 32 deep
read n_available samples (6 bytes each), push raw red/ir into ring buffers
```

Ring buffers are **file-scope static** (not on the stack): `int32_t red[N]`,
`int32_t ir[N]` with `N = 768` (~15 s at 50 Hz) ≈ 6 KB total. The sampler keeps
a write index and a running fill count.

### Analysis (recomputed every ~500 ms)

Run over the most recent filled window (up to ~10 s). All math in `float`, using
a static scratch buffer (not stack).

1. **Finger detection.** `ir_dc = mean(raw IR window)`. If
   `ir_dc < FINGER_IR_THRESHOLD` (≈ 50 000, tuned empirically) → no finger →
   `valid = false`, `hr = spo2 = 0`. Done.
2. **Heart rate.**
   - DC-block the IR signal with a one-pole high-pass:
     `w[n] = ir[n] - ir[n-1] + 0.995 * w[n-1]` → pulsatile AC component.
   - Smooth with a short (~5-tap) moving average to suppress high-frequency noise.
   - **Peak detect:** local maxima above an adaptive threshold (a fraction of the
     window's recent peak-to-peak amplitude), separated by a refractory period
     ≥ 300 ms (15 samples) → caps at ~200 BPM.
   - Inter-beat intervals (IBI) in ms = `samples_between_peaks * (1000/50)`.
     Discard IBIs outside [300, 2000] ms (30–200 BPM). `BPM = 60000 / median(IBI)`.
   - **Confidence gate:** require ≥ 4 accepted beats AND IBI coefficient of
     variation < ~0.25. Otherwise `valid = false` (honest: noise → no number).
3. **SpO2 estimate** (only when HR is valid).
   - `dc_ir = mean(raw IR)`, `ac_ir = max-min of the AC-filtered IR` over the
     window; same for RED.
   - `R = (ac_red / dc_red) / (ac_ir / dc_ir)` (guard against divide-by-zero).
   - `spo2 = 104 - 17 * R`, clamped to [70, 100]. Stored as an integer; the GUI
     adds the `(estimate)` label.

### Shared state and concurrency

```c
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static sensor_vitals_t s_vitals;   // {uint16_t hr; uint8_t spo2; bool valid}
```

The sampler writes `s_vitals` under `s_mux`; `sensor_read_vitals()` copies it
out under `s_mux`. The struct is tiny so critical sections are negligible.

### Development debug dump (LOG plane only)

The sampler `ESP_LOGI`s a one-line summary every ~1–2 s on the USB-Serial-JTAG
**log plane** (COM13): `ir_dc`, beats found, BPM, `R`, SpO2, `valid`. This never
touches the DATA plane / binary protocol. An optional throwaway Python script
can parse these lines and plot the waveform + detected beats with matplotlib for
the report figure (matplotlib is **not** added as a production dependency).

### PC measurement flow

`serial_bridge.py` gains:

```python
def measure_vitals(self, duration_s=15.0, settle_s=2.0, on_update=None) -> Vitals
```

It polls `read_sensor()` (~every 0.4 s), forwarding each `{hr, spo2, valid}` to
`on_update` for live display. It returns early once readings are `valid` and
stable (last few within ±3 BPM) for `settle_s`, otherwise after `duration_s`.
The existing instant `read_sensor()` is kept and reused as the poll primitive.

`gui.py`:
- The **"Read vitals"** button runs `measure_vitals` on a background thread,
  disabled while running, with a `ttk.Progressbar` and live text
  (`"Measuring… keep fingertip still"` → live BPM → final
  `"HR: 78 BPM   SpO2: ~97% (estimate)"`).
- On a poor result: `"Could not get a reliable reading. Place fingertip gently
  and keep still."` — never a fabricated number.
- **`_connect` no longer auto-runs a full measurement** (it would block 12–15 s).
  It shows the hint `"Press 'Read vitals' and place fingertip on MAX30102"`.
- SpO2 is always rendered as `~X% (estimate)`.

## Risk & Edge Cases

| Case | Behaviour |
|---|---|
| No finger | `ir_dc` below threshold → `valid = false`; GUI shows the hint. |
| Motion artifact | Irregular IBIs → confidence gate fails → `valid = false`; honest "no reliable reading". |
| Finger placed mid-measurement | `valid` flips true once ≥4 consistent beats accrue; PC settle logic handles it. |
| HR out of range | IBIs outside [300, 2000] ms discarded; if none remain → invalid. |
| Sampler falls behind | Rollover ON → HW FIFO overwrites oldest, never freezes; analysis uses what's buffered. |
| I2C read error | Sampler skips that tick; retries next wake. |
| `dc_ir`/`ac_ir` ≈ 0 | SpO2 divide guarded; SpO2 = 0 / invalid rather than NaN. |
| Two readers of vitals | Only the sampler does I2C; `READ_SENSOR` reads shared state. No bus contention. |

## Verification

1. **LOG-plane bring-up.** Flash, then `idf.py -p COM13 monitor`. With no finger,
   `ir_dc` is low and `valid=false`. Place fingertip: `ir_dc` jumps, beats are
   detected, BPM stabilises within a few seconds.
2. **Manual-count accuracy (acceptance).** Hold fingertip still ~15 s; hand-count
   pulse for 30 s ×2. ESP BPM must agree within **±5 BPM** and be repeatable.
3. **No-finger honesty.** Remove finger → `valid=false`; GUI shows the hint, no
   number.
4. **GUI end-to-end.** "Read vitals" shows progress + live BPM + final value with
   the `(estimate)` SpO2 label. A poor signal yields the "no reliable reading"
   message.
5. **Regression.** Voice record → transcribe → reply → speaker playback still
   works (audio path untouched); during recording the LLM now receives real
   vitals.

## Out of Scope (Backlog)

- SpO2 calibration against a commercial pulse oximeter.
- Approach B (stream raw PPG to PC) for a live in-GUI waveform.
- FIFO-almost-full interrupt on GPIO 4 instead of polling.
- Project-wide Turkish→English comment cleanup (`main.c`, `serial_bridge.py`,
  `config.h`, etc.).
- HRV and other derived metrics.
