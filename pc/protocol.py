"""Binary frame protocol — sync, CRC16-CCITT, message types."""

from __future__ import annotations

import struct
from typing import Callable, Optional

SYNC0, SYNC1 = 0xAA, 0x55

MSG_AUDIO_UP = 0x01
MSG_SENSOR = 0x02
MSG_BUTTON = 0x03
MSG_STATUS = 0x04
MSG_WAKE_DETECTED = 0x05
MSG_HEARTBEAT = 0x06
MSG_AUDIO_DOWN = 0x10
MSG_LED_CMD = 0x11
MSG_PLAYBACK_END = 0x12
MSG_RESET = 0x13
MSG_CRYPTO_CAP = 0x14
MSG_START_RECORD = 0x15
MSG_STOP_RECORD = 0x16
MSG_READ_SENSOR = 0x17

ENCRYPTED_TYPES = {MSG_AUDIO_UP, MSG_SENSOR, MSG_AUDIO_DOWN}


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def pack_frame(msg_type: int, payload: bytes = b"") -> bytes:
    header = struct.pack(">HB", len(payload), msg_type)
    body = header[2:] + payload  # type + payload
    c = crc16(body)
    return bytes([SYNC0, SYNC1]) + header[:2] + body + struct.pack(">H", c)


class FrameParser:
    def __init__(self, on_frame: Callable[[int, bytes], None]) -> None:
        self._on_frame = on_frame
        self._buf = bytearray()
        self._state = "sync"
        self._payload_len = 0
        self._msg_type = 0

    def feed(self, data: bytes) -> None:
        for b in data:
            if self._state == "sync":
                if len(self._buf) == 0 and b == SYNC0:
                    self._buf.append(b)
                elif len(self._buf) == 1 and b == SYNC1:
                    self._buf.append(b)
                    self._state = "header"
                else:
                    self._buf.clear()
                    if b == SYNC0:
                        self._buf.append(b)
            elif self._state == "header":
                self._buf.append(b)
                if len(self._buf) == 4:
                    self._payload_len = (self._buf[2] << 8) | self._buf[3]
                    if self._payload_len > 4096:
                        self._reset()
                    else:
                        self._state = "body"
            elif self._state == "body":
                self._buf.append(b)
                need = 1 + self._payload_len + 2
                if len(self._buf) - 4 >= need:
                    self._dispatch()
                    self._reset()

    def _dispatch(self) -> None:
        msg_type = self._buf[4]
        payload = bytes(self._buf[5 : 5 + self._payload_len])
        recv_crc = (self._buf[5 + self._payload_len] << 8) | self._buf[5 + self._payload_len + 1]
        body = bytes([msg_type]) + payload
        if crc16(body) != recv_crc:
            return
        self._on_frame(msg_type, payload)

    def _reset(self) -> None:
        self._buf.clear()
        self._state = "sync"
        self._payload_len = 0
