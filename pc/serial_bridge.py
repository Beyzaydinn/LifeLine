"""USB serial bridge to ESP32 — threaded RX, frame TX."""

from __future__ import annotations

import logging
import threading
import time
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
    def __init__(self, port: Optional[str] = None, baud: int = 115200) -> None:
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
        self._ser = serial.Serial(self.port, self.baud, timeout=0.05)
        time.sleep(2.0)

    def close(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()

    def wait_crypto_cap(self, timeout: float = 10.0) -> None:
        self._crypto_ready.wait(timeout)

    def _on_frame(self, msg_type: int, payload: bytes) -> None:
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
            except Exception as e:
                log.warning("audio decrypt: %s", e)
        elif msg_type == MSG_STATUS and len(payload) >= 2:
            if self._status_cb:
                self._status_cb(payload[0], payload[1])

    def _reader_loop(self) -> None:
        assert self._ser
        while self._ser.is_open:
            data = self._ser.read(512)
            if data:
                self._parser.feed(data)

    def start_reader(self) -> threading.Thread:
        t = threading.Thread(target=self._reader_loop, daemon=True)
        t.start()
        return t

    def send_frame(self, msg_type: int, payload: bytes = b"", encrypt: bool = False) -> None:
        if encrypt or msg_type in ENCRYPTED_TYPES:
            payload = encrypt_payload(payload)
        frame = pack_frame(msg_type, payload)
        with self._lock:
            assert self._ser
            self._ser.write(frame)

    def start_recording(self) -> None:
        self._audio_chunks.clear()
        self._recording = True
        self.send_frame(MSG_START_RECORD, b"", encrypt=False)

    def stop_recording(self) -> bytes:
        self.send_frame(MSG_STOP_RECORD, b"", encrypt=False)
        time.sleep(0.5)
        self._recording = False
        pcm = b"".join(self._audio_chunks)
        log.info("Collected %d bytes PCM", len(pcm))
        return pcm

    def read_sensor(self) -> Vitals:
        self.send_frame(MSG_READ_SENSOR, b"", encrypt=False)
        time.sleep(0.3)
        return self._last_vitals

    def last_vitals(self) -> Vitals:
        return self._last_vitals

    def send_audio_down(self, pcm: bytes) -> None:
        from protocol import MSG_AUDIO_DOWN

        self.send_frame(MSG_AUDIO_DOWN, pcm, encrypt=True)

    def send_playback_end(self) -> None:
        from protocol import MSG_PLAYBACK_END

        self.send_frame(MSG_PLAYBACK_END, b"", encrypt=False)
