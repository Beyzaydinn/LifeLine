"""AES-256-CBC + PKCS#7 — matches ESP32 firmware (demo key/IV)."""

from __future__ import annotations

import os
from Cryptodome.Cipher import AES

CRYPTO_FLAG_ENCRYPTED = 0x01

AES_KEY = bytes([
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
])

AES_IV_DEFAULT = bytes([
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
])


def pkcs7_pad(data: bytes) -> bytes:
    pad = 16 - (len(data) % 16)
    return data + bytes([pad] * pad)


def pkcs7_unpad(data: bytes) -> bytes:
    if not data or len(data) % 16 != 0:
        raise ValueError("invalid padded data")
    pad = data[-1]
    if pad < 1 or pad > 16 or pad > len(data):
        raise ValueError("invalid padding")
    if data[-pad:] != bytes([pad] * pad):
        raise ValueError("invalid padding bytes")
    return data[:-pad]


def aes_encrypt_cbc(plaintext: bytes, iv: bytes) -> bytes:
    cipher = AES.new(AES_KEY, AES.MODE_CBC, iv)
    return cipher.encrypt(pkcs7_pad(plaintext))


def aes_decrypt_cbc(ciphertext: bytes, iv: bytes) -> bytes:
    cipher = AES.new(AES_KEY, AES.MODE_CBC, iv)
    return pkcs7_unpad(cipher.decrypt(ciphertext))


def encrypt_payload(plain: bytes) -> bytes:
    iv = os.urandom(16)
    ct = aes_encrypt_cbc(plain, iv)
    return bytes([CRYPTO_FLAG_ENCRYPTED]) + iv + ct


def decrypt_payload(blob: bytes) -> bytes:
    if not blob:
        return b""
    if (blob[0] & CRYPTO_FLAG_ENCRYPTED) == 0:
        return blob
    if len(blob) < 17:
        raise ValueError("encrypted blob too short")
    iv = blob[1:17]
    ct = blob[17:]
    return aes_decrypt_cbc(ct, iv)


def self_test() -> bool:
    msg = b"Hello ESP32-S3! This is an AES-256-CBC test message."
    ct = aes_encrypt_cbc(msg, AES_IV_DEFAULT)
    pt = aes_decrypt_cbc(ct, AES_IV_DEFAULT)
    return pt == msg
