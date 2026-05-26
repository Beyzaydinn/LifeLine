"""USB serial bridge to ESP32 — threaded RX, frame TX."""

from __future__ import annotations

import logging
import threading
import time
from collections import Counter
from dataclasses import dataclass
from typing import Callable, List, Optional

import serial
from serial.tools import list_ports

from crypto_aes import decrypt_payload, encrypt_payload
from protocol import (
    ENCRYPTED_TYPES,
    MSG_AUDIO_UP,
    MSG_CRYPTO_CAP,
    MSG_HEARTBEAT,
    MSG_READ_SENSOR,
    MSG_SENSOR,
    MSG_START_RECORD,
    MSG_STATUS,
    MSG_STOP_RECORD,
    FrameParser,
    pack_frame,
)

log = logging.getLogger(__name__)


@dataclass
class Vitals:
    heart_rate: int = 0
    spo2: int = 0
    valid: bool = False


class SerialBridge:
    def __init__(self, port: Optional[str] = None, baud: int = 921600) -> None:
        self.port = port or self._auto_port()
        self.baud = baud
        self._ser: Optional[serial.Serial] = None
        self._lock = threading.Lock()
        self._audio_chunks: List[bytes] = []
        self._recording = False
        self._last_vitals = Vitals()
        self._crypto_ready = threading.Event()
        self._status_cb: Optional[Callable[[int, int], None]] = None
        self._parser = FrameParser(self._on_frame)
        # DIAG: her gelen frame'i tipe gore say. stop_recording'de ozet bas.
        # Bu fix degil olcu aleti; bug bulunduktan sonra kaldirilabilir.
        self._frame_counts: Counter = Counter()
        # Audio playback pacing: ESP playback queue 8 deep ve I2S 62.5 chunk/sn
        # drain ediyor (16 ms/chunk). UART burst hizinda (~171 chunk/sn)
        # gondersek queue overflow + drop olur, PLAYBACK_END dahil. Burada
        # gercek-zamanli besleme yapip queue'yu yakin-bos tutariz.
        self._audio_ref_t: float = 0.0
        self._audio_idx: int = 0
        self._audio_last_t: float = 0.0

    def set_status_callback(self, cb: Callable[[int, int], None]) -> None:
        self._status_cb = cb

    @staticmethod
    def list_ports() -> list[str]:
        found = []
        for p in list_ports.comports():
            print(f"  {p.device}: {p.description}")
            found.append(p.device)
        return found

    @staticmethod
    def _auto_port() -> str:
        ports = list(list_ports.comports())
        for p in ports:
            desc = (p.description or "").lower()
            if "usb serial device" in desc:
                return p.device
        for p in ports:
            desc = (p.description or "").lower()
            if "usb" in desc and "jtag" not in desc and "cp210" not in desc:
                return p.device
        if not ports:
            raise RuntimeError("No serial port found. Connect ESP32 USB OTG.")
        return ports[-1].device

    def open(self) -> None:
        # Windows USB CDC: PySerial varsayilan parametreleri ile bazen
        # "device does not recognize command" hatasi veriyor. Acik parametreler +
        # acilis sonrasi bekleme + buffer temizligi ile sorun cozuluyor.
        self._ser = serial.Serial(
            port=self.port,
            baudrate=self.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            write_timeout=2.0,
            rtscts=False,
            dsrdtr=False,
            xonxoff=False,
        )
        # USB CDC endpoint'i hazir olana kadar bekle (1.5 sn pratikte yetiyor)
        time.sleep(1.5)
        try:
            self._ser.reset_input_buffer()
            self._ser.reset_output_buffer()
        except serial.SerialException:
            pass

    def close(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()

    def wait_crypto_cap(self, timeout: float = 10.0) -> None:
        self._crypto_ready.wait(timeout)

    def _on_frame(self, msg_type: int, payload: bytes) -> None:
        self._frame_counts[msg_type] += 1
        if msg_type == MSG_CRYPTO_CAP:
            self._crypto_ready.set()
        elif msg_type == MSG_SENSOR:
            try:
                plain = decrypt_payload(payload)
                if len(plain) >= 4:
                    hr = (plain[0] << 8) | plain[1]
                    self._last_vitals = Vitals(hr, plain[2], plain[3] == 1)
            except Exception as e:
                log.warning("sensor decrypt: %s", e)
        elif msg_type == MSG_AUDIO_UP and self._recording:
            try:
                self._audio_chunks.append(decrypt_payload(payload))
                # Her 30 chunk'ta bir akis durumu logla (~her saniye).
                # Test sirasinda PowerShell'de gercek zamanli gorulur.
                if len(self._audio_chunks) % 30 == 0:
                    total_kb = sum(len(c) for c in self._audio_chunks) // 1024
                    log.info("Audio: %d chunks, ~%d KB so far",
                             len(self._audio_chunks), total_kb)
            except Exception as e:
                log.warning("audio decrypt: %s", e)
        elif msg_type == MSG_STATUS and len(payload) >= 2:
            if self._status_cb:
                self._status_cb(payload[0], payload[1])

    def _reader_loop(self) -> None:
        assert self._ser
        while self._ser is not None and self._ser.is_open:
            try:
                data = self._ser.read(512)
                if data:
                    self._parser.feed(data)
            except serial.SerialException as e:
                log.warning("Serial read error (retrying): %s", e)
                time.sleep(0.1)
            except Exception as e:
                log.error("Unexpected reader error: %s", e)
                time.sleep(0.1)

    def start_reader(self) -> threading.Thread:
        t = threading.Thread(target=self._reader_loop, daemon=True)
        t.start()
        return t

    def send_frame(self, msg_type: int, payload: bytes = b"", encrypt: bool = False) -> None:
        if encrypt or msg_type in ENCRYPTED_TYPES:
            payload = encrypt_payload(payload)
        frame = pack_frame(msg_type, payload)
        with self._lock:
            if self._ser is None or not self._ser.is_open:
                log.warning("send_frame: port closed, frame dropped (type=0x%02X)", msg_type)
                return
            try:
                self._ser.write(frame)
            except serial.SerialException as e:
                log.warning("Serial write error: %s", e)

    def start_recording(self) -> None:
        self._audio_chunks.clear()
        self._recording = True
        self.send_frame(MSG_START_RECORD, b"", encrypt=False)

    def stop_recording(self) -> bytes:
        self.send_frame(MSG_STOP_RECORD, b"", encrypt=False)
        # 1.5 sn bekle: ESP32 STOP_RECORD'u alip task_audio_in'i durdurana
        # kadar gecen kuyruktaki son chunk'lar da PC'ye ulasmali. 0.5 sn
        # kisa kaliyordu ve son ~30 chunk kayboluyordu.
        time.sleep(1.5)
        self._recording = False
        pcm = b"".join(self._audio_chunks)
        log.info("Collected %d bytes PCM (~%.1f s of audio at 16 kHz)",
                 len(pcm), len(pcm) / (16000 * 2))
        # DIAG: bu oturum boyunca PC'ye gelen tum frame'lerin tip dagilimi
        type_names = {
            0x01: "AUDIO_UP", 0x02: "SENSOR", 0x03: "BUTTON", 0x04: "STATUS",
            0x05: "WAKE_DET", 0x06: "HEARTBEAT", 0x10: "AUDIO_DOWN",
            0x11: "LED_CMD", 0x12: "PLAYBACK_END", 0x13: "RESET",
            0x14: "CRYPTO_CAP", 0x15: "START_REC", 0x16: "STOP_REC",
            0x17: "READ_SENSOR",
        }
        pretty = {type_names.get(t, f"0x{t:02X}"): c for t, c in self._frame_counts.items()}
        log.info("DIAG frame counts since connect: %s", pretty)
        return pcm

    def read_sensor(self) -> Vitals:
        self.send_frame(MSG_READ_SENSOR, b"", encrypt=False)
        time.sleep(0.3)
        return self._last_vitals

    def last_vitals(self) -> Vitals:
        return self._last_vitals

    def send_audio_down(self, pcm: bytes) -> None:
        from protocol import MSG_AUDIO_DOWN

        # Real-time pacing: 256 sample @ 16 kHz = 16 ms/chunk. Yeni oturum
        # tespiti: son chunk'tan beri >1 sn gecmisse referansi sifirla.
        now = time.perf_counter()
        if self._audio_idx == 0 or (now - self._audio_last_t) > 1.0:
            self._audio_ref_t = now
            self._audio_idx = 0
        chunk_s = len(pcm) / 2 / 16000  # tipik 0.016
        target_t = self._audio_ref_t + self._audio_idx * chunk_s
        delay = target_t - now
        if delay > 0:
            time.sleep(delay)
        self._audio_idx += 1
        self._audio_last_t = time.perf_counter()
        self.send_frame(MSG_AUDIO_DOWN, pcm, encrypt=True)

    def send_playback_end(self) -> None:
        from protocol import MSG_PLAYBACK_END

        self.send_frame(MSG_PLAYBACK_END, b"", encrypt=False)
