# LifeLine — CEN322 Offline Voice First-Aid Assistant

University IoT course (CEN322) final project. A fully offline, voice-controlled
first-aid assistant. No internet/API.

## Language convention

The course is in English, so all project artifacts are in English: code,
comments, log messages, commit messages, README, the report, and these docs.
Converse with the user in Turkish, but never put Turkish into the project itself.

## Two-machine architecture

The work is split across two devices that talk over USB-UART with an encrypted
binary protocol.

- **ESP32-S3-DevKitM-1-N8** (`firmware/`): microphone capture (INMP441 I2S),
  speaker output (MAX98357A I2S), MAX30102 sensor (I2C), WS2812B NeoPixel
  status LED (RMT). ESP-IDF v5.5.3 + FreeRTOS.
- **PC application** (`pc/`): Faster-Whisper STT + Ollama Llama 3.2 3B +
  Piper TTS. Tkinter GUI (`gui.py`). All AI is local.

Data path: PC `gui.py` → START/STOP to the ESP for mic recording → ESP streams
raw PCM in chunks → Whisper transcript → Llama reply → Piper TTS → AES'd audio
chunks to the ESP → MAX98357A speaker.

## Two-plane I/O (important)

The ESP has two separate USB connections, for separate jobs:

- **DATA plane**: UART0 (GPIO 43 TX / 44 RX, 921600 baud) → on-board
  CP210x/CH9102 USB-UART bridge → a COM port on the PC (e.g. COM12). ONLY the
  framed binary protocol (audio + sensor payloads AES'd; control/status frames
  are cleartext). NO console/log.
- **LOG plane**: USB-Serial-JTAG (native USB connector) → a separate COM port
  (e.g. COM13). ESP_LOGx output and panic traces. `idf.py -p COMxx monitor`.

The two planes share neither wires nor driver; log traffic cannot corrupt the
binary stream. (Details: header comment of `firmware/sdkconfig.defaults`.)

## Build / Flash / Monitor (ESP)

With the ESP-IDF v5.5.3 environment active, from the `firmware/` directory:

```
idf.py build flash          # build + flash (DATA port, e.g. COM12)
idf.py -p COM13 monitor     # watch logs (LOG port, USB-JTAG)
```

ESP-IDF install: `C:\esp\v5.5.3\esp-idf`.

## Run / Test (PC)

Python venv: `pc/.venv/`. Always use the venv python:

```
cd pc
.\.venv\Scripts\python.exe gui.py              # main GUI application
.\.venv\Scripts\python.exe _test_tts_calm.py   # TTS smoke (produces a WAV)
.\.venv\Scripts\python.exe _test_esp_playback.py  # end-to-end ESP playback timing
.\.venv\Scripts\python.exe diag_raw_uart.py    # raw UART byte sniffer
```

Dependencies: `pc/requirements.txt` (pyserial, pycryptodomex, faster-whisper,
piper-tts, nvidia-cublas-cu12). Models live in `pc/models/` (gitignored): Piper
`.onnx` + Whisper `faster-whisper-small.en/`.

## Speech-to-text (Whisper)

`stt.py` is fully offline: `HF_HUB_OFFLINE` + `local_files_only` keep it off the
network; it loads the model locally (otherwise falls back to the HF cache).

- **GPU first, CPU fallback.** `_get_model()` tries `device="cuda",
  compute_type="int8_float16"` and falls back to `device="cpu",
  compute_type="int8"` on any failure, so the demo never breaks. On the RTX 3050
  (4 GB) GPU transcribe is ~0.16 s vs ~1.87 s on CPU; int8_float16 keeps the VRAM
  footprint small (~0.3 GB) so Whisper coexists with Llama (~2.8 GB in Ollama).
- **cuBLAS DLL gotcha (Windows).** CTranslate2 ships cuDNN but not cuBLAS. The
  `nvidia-cublas-cu12` wheel provides `cublas64_12.dll`. CTranslate2 loads cublas
  lazily BY NAME, and that search uses PATH, NOT `os.add_dll_directory`, so
  `_register_cuda_dll_dirs()` puts the wheel bin dirs on `os.environ["PATH"]`.
  Without it you get "cublas64_12.dll is not found" and silently fall back to CPU.
- **Warm-up + lock.** `_build_model()` runs a 1 s silent warm-up so GPU/DLL errors
  surface at load time (not mid-recording) and the first real call is fast.
  `_get_model()` is guarded by `_model_lock` (double-checked) so the GUI startup
  prewarm thread and the transcribe thread cannot load the model twice. The GUI
  calls `stt.prewarm()` in a background thread at startup.

## Audio format (critical detail)

- Microphone & speaker: **16 kHz mono int16**. 22050 Hz was tried; the
  MAX98357A/ESP32 I2S driver could not derive it cleanly.
- ESP I2S TX (`audio_output.c`): **STEREO 32-bit slot**, each mono sample is
  duplicated to L+R and written `(int32)<<16` MSB-aligned. Symmetric with
  `audio_input.c`. The ESP-IDF v5 driver does NOT auto-pad when
  data_bit_width < slot_bit_width — skip this and you get chipmunk audio
  (2× speed + 1 octave up).
- PC `serial_bridge.send_audio_down`: sends audio at **real-time rate**
  (16 ms/chunk). Sending at UART burst rate overflows the ESP playback queue
  (depth 8) and drops chunks.
- TTS tempo: `LENGTH_SCALE` (1.25) and `SENTENCE_SILENCE_S` (0.45) constants at
  the top of `pc/tts.py`. Passed to Piper's SynthesisConfig.

## Encryption

AES-256-CBC + PKCS#7, HW-accelerated via mbedtls on the ESP (`crypto_aes.c`),
pycryptodomex on the PC (`crypto_aes.py`). **This encryption stays — required by
the course.** Demo key/IV (key `0x00..0x1f` hardcoded both sides), not production.

- **Coverage is partial by design**: only `AUDIO_UP` (0x01), `AUDIO_DOWN` (0x10)
  and `SENSOR` (0x02) payloads are encrypted (`protocol_type_encrypted()` /
  PC `ENCRYPTED_TYPES`). All control/status frames (HEARTBEAT, STATUS,
  CRYPTO_CAP, START/STOP_RECORD, READ_SENSOR, PLAYBACK_END, RESET) are cleartext.
- IV is random per message (`esp_fill_random` / `os.urandom`), prepended on the
  wire as `[flags(1)=0x01][IV(16)][ciphertext]`. The fixed `AES_IV_DEFAULT` is
  used ONLY by the crypto self-test, never on the live link. No MAC/auth tag —
  confidentiality only; frame integrity comes from the (non-crypto) CRC16.

## Rules / constraints

- ESP-IDF is used (not Arduino); the instructor approved this.
- AES encryption will not be removed (course requirement).
- No API keys, no network code — fully offline.
- All project artifacts in English; converse with the user in Turkish (see
  "Language convention").
- Instructor's assignment brief: `docs/instructions/homework_instructions.md` +
  the PDF report templates.

## Directory layout

```
firmware/main/    ESP-IDF source (main.c FSM + tasks, audio_*, sensor, crypto, uart_link, protocol, led_strip_ctrl)
firmware/sdkconfig.defaults   two-plane I/O config + AES HW + FreeRTOS
pc/               PC side (gui, stt, llm, tts, serial_bridge, crypto_aes, protocol)
pc/_test_*.py     diagnostic/test scripts
docs/             reports (.md), instructions/, superpowers/specs/ (design docs)
wokwi/            simulation (diagram.json + sketch.ino NeoPixel-FSM demo) — DevKitC-1 stand-in board + stand-in parts (pot=mic, buzzer=speaker, MPU6050=MAX30102); sim pins now MATCH config.h (NeoPixel 38, mic/pot 2, speaker/buzzer 7, I2C 8/9/4). project_link.txt is still a placeholder URL.
```

## Notes

- `firmware/main/usb_cdc.c/h`: leftover from the old USB-OTG CDC approach; UART is
  used now (abandoned due to Windows TinyUSB CDC incompatibility).
- MAX30102 vitals are REAL PPG, not placeholders (the old "fake/modulo" note was
  stale). `sensor.c` drains the FIFO (RED then IR, 18-bit) with two-stage
  finger-gating (raw IR ≥ 50000); `ppg.c` does one-pole high-pass DC removal
  (α=0.97), 5-tap smoothing, refractory-gated peak detection (0.5 s → rejects the
  dicrotic notch), IBI filtering + coefficient-of-variation gate, median IBI →
  BPM (clamped 30–200), and a ratio-of-ratios SpO2 estimate
  (`R = (AC_red/DC_red)/(AC_ir/DC_ir)`, `SpO2 = 104 − 17·R`, clamped 70–100), plus
  a boot `ppg_self_test()`. SpO2 is NOT factory-calibrated, so it is surfaced to
  the LLM as an "uncalibrated estimate"; only clinical calibration remains future
  work. Effective sample rate 50 Hz, ~12 s analysis window, recomputed ~every
  480 ms.
