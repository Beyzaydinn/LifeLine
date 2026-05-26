"""DIAG: WAV dosyasi (tts_calm_test.wav) PCM'ini dogrudan ESP hoparlorune
gonderir. STT/LLM/TTS atlanir; ayni PCM hem WAV oynaticisi hem ESP'de
calar, sure kiyaslamasi yapilabilir.

Kullanim:
    python _test_esp_playback.py [--port COM12]

Cikti:
    WAV header sure: ... sn (bilinen referans)
    ESP'ye gonderim basladi -> ESP IDLE'a dondu: ... sn (olculen)
    Oran = olculen / beklenen  (1.0 = perfect, <1.0 = hizli, >1.0 = yavas)

Bu olcum hipotezi keser:
- Oran ~1.0: fix dogru calisiyor; tonal/distortion sorunu kaliyor
- Oran ~0.5: hala 2x hizli (chipmunk donmedi)
- Oran ~2.0: fix overshoot yapti (2x yavasladi)
"""

from __future__ import annotations

import argparse
import logging
import sys
import threading
import time
import wave
from pathlib import Path

from serial_bridge import SerialBridge

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("esp_playback")

WAV_PATH = Path(__file__).parent / "tts_calm_test.wav"
CHUNK_BYTES = 512


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None)
    args = ap.parse_args()

    if not WAV_PATH.exists():
        log.error("Once `python _test_tts_calm.py` calistirip WAV uret.")
        return 1

    # WAV header oku - referans sure
    with wave.open(str(WAV_PATH), "rb") as w:
        sr = w.getframerate()
        ch = w.getnchannels()
        sw = w.getsampwidth()
        n_frames = w.getnframes()
        wav_duration = n_frames / sr
        pcm = w.readframes(n_frames)

    log.info("WAV: %d Hz, %d ch, %d-bit, %d frames", sr, ch, sw * 8, n_frames)
    log.info("WAV sure (referans): %.3f sn", wav_duration)
    log.info("Toplam PCM byte: %d, %d adet %d-byte chunk olarak gonderilecek",
             len(pcm), (len(pcm) + CHUNK_BYTES - 1) // CHUNK_BYTES, CHUNK_BYTES)

    # ESP'ye baglan
    bridge = SerialBridge(port=args.port)
    bridge.open()
    log.info("Connected to %s", bridge.port)
    bridge.start_reader()
    bridge.wait_crypto_cap(8)
    log.info("Crypto handshake done.")

    # STATUS callback'i: IDLE'a donus zamanini yakala
    idle_event = threading.Event()

    def on_status(state: int, _err: int) -> None:
        # 0 = IDLE, 3 = SPEAKING (state_machine.h'den)
        if state == 0 and not idle_event.is_set():
            idle_event.set()
            log.info("ESP STATE_IDLE'a dondu (PLAYBACK_END alindi)")

    bridge.set_status_callback(on_status)

    # ESP zaten IDLE'da olmali, ama emin olmak icin event'i sifirla
    # NOT: ilk STATUS gelmis olabilir, idle_event set olmus olabilir.
    # Bunu sifirla ve audio_down'dan once SPEAKING'e gectigini bekle.
    idle_event.clear()

    log.info("=" * 60)
    log.info(">>> ESP'ye PCM gonderiliyor — STOPWATCH BASLAT")
    log.info("=" * 60)
    t0 = time.perf_counter()

    # Chunk'lara bol, gonder
    for i in range(0, len(pcm), CHUNK_BYTES):
        chunk = pcm[i:i + CHUNK_BYTES]
        bridge.send_audio_down(chunk)

    t_send_done = time.perf_counter()
    log.info("Tum chunk'lar UART'a yazildi (%.3f sn). Simdi PLAYBACK_END gonderiliyor.",
             t_send_done - t0)

    # PLAYBACK_END gonder - ESP queue'yu drain edip IDLE'a donmeli
    bridge.send_playback_end()

    # IDLE'i bekle - timeout uzun: eger ESP I2S yarı hızda çalıyorsa
    # playback ~19 sn surer, 60 sn ile bekleyelim.
    log.info("ESP IDLE bekleniyor (timeout %.0f sn, her saniye . yazilacak)...",
             wav_duration + 50)
    deadline = time.time() + wav_duration + 50
    next_tick = time.time() + 1.0
    while not idle_event.is_set() and time.time() < deadline:
        if time.time() >= next_tick:
            elapsed = time.time() - (t_send_done if 't_send_done' in dir() else t0)
            print(f"  ... {elapsed:.1f} sn (PLAYBACK_END'den sonra)", flush=True)
            next_tick = time.time() + 2.0
        idle_event.wait(timeout=0.5)

    if idle_event.is_set():
        t1 = time.perf_counter()
        measured = t1 - t0
        log.info("=" * 60)
        log.info(">>> HOPARLOR BITTI (ESP IDLE)")
        log.info("=" * 60)
        log.info("Olculen sure: %.3f sn", measured)
        log.info("WAV referans:  %.3f sn", wav_duration)
        ratio = measured / wav_duration
        log.info("Oran (olculen / WAV): %.3f", ratio)
        if 0.9 <= ratio <= 1.1:
            log.info("  => ~1.0: fix hızı düzeltti, distortion baska sebep")
        elif ratio < 0.6:
            log.info("  => ~0.5: hala chipmunk (2x hizli)")
        elif ratio > 1.5:
            log.info("  => ~2.0: fix overshoot, 2x yavaşlamıs")
        else:
            log.info("  => Beklenen kategorilerin dişında, başka neden")
    else:
        log.error("Timeout: ESP IDLE'a %.0f sn icinde donmedi.", wav_duration + 50)
        log.error("Iki olasilik:")
        log.error("  1. PLAYBACK_END dropped (queue overflow halen var)")
        log.error("  2. ESP I2S clock yanlis hizda, playback %.0fsn+ surdu",
                  wav_duration + 50)
        return 1

    bridge.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
