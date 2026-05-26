"""DIAG: ESP32 UART hatti gercekten data uretiyor mu? Frame parsing'i atlat,
sadece ham byte'lari say ve hex olarak ilk N byte'i bas.

Kullanim:
    python diag_raw_uart.py [--port COM12] [--baud 921600] [--seconds 10]

Yorumlama:
- 0 byte gelirse: ESP sessiz veya port yanlis veya baud yanlis.
- Bayt geliyor ama '\\xaa\\x55' yoksa: framing/baud bozuk, ESP yaziyor ama
  bizim parser senkronize olamiyor.
- '\\xaa\\x55' goruluyorsa: transport saglam, daha ust katmana bakacagiz.

Birden fazla baud denemek icin: --baud-scan
"""

from __future__ import annotations

import argparse
import sys
import time

import serial
from serial.tools import list_ports


def list_all_ports() -> None:
    print("=== Tum COM portlari ===")
    for p in list_ports.comports():
        print(f"  {p.device:8s}  {p.description}  [{p.hwid}]")
    print()


def sniff(port: str, baud: int, seconds: float) -> None:
    print(f"--- {port} @ {baud} baud, {seconds:.1f} sn dinleniyor ---")
    try:
        ser = serial.Serial(
            port=port,
            baudrate=baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            rtscts=False,
            dsrdtr=False,
            xonxoff=False,
        )
    except serial.SerialException as e:
        print(f"  ACILMADI: {e}")
        return

    # ESP USB-UART bridge bazen DTR/RTS ile reset eder. Acilis sonrasi
    # ~1.5 sn bekle ki ESP app_main'e ulassin.
    time.sleep(1.5)
    try:
        ser.reset_input_buffer()
    except Exception:
        pass

    total = 0
    sync_count = 0
    first_chunk = b""
    deadline = time.time() + seconds
    while time.time() < deadline:
        data = ser.read(1024)
        if data:
            total += len(data)
            if not first_chunk:
                first_chunk = data[:64]
            # 0xAA 0x55 sync word say
            i = 0
            while i < len(data) - 1:
                if data[i] == 0xAA and data[i + 1] == 0x55:
                    sync_count += 1
                    i += 2
                else:
                    i += 1

    ser.close()
    print(f"  Toplam byte: {total}")
    print(f"  0xAA 0x55 sync word sayisi: {sync_count}")
    if first_chunk:
        print(f"  Ilk {len(first_chunk)} byte (hex): {first_chunk.hex(' ')}")
        # ASCII gorunumu — boot ROM mesaji veya plain text varsa anlasilir
        try:
            ascii_view = first_chunk.decode("ascii", errors="replace")
            print(f"  Ilk {len(first_chunk)} byte (ascii): {ascii_view!r}")
        except Exception:
            pass
    else:
        print("  (hicbir byte gelmedi)")
    print()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None, help="COM port (otomatik denenir)")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--seconds", type=float, default=8.0)
    ap.add_argument(
        "--baud-scan",
        action="store_true",
        help="115200, 460800, 921600 baud sirayla dene",
    )
    args = ap.parse_args()

    list_all_ports()

    if args.port is None:
        ports = [p.device for p in list_ports.comports()]
        if not ports:
            print("Hicbir COM port bulunamadi.")
            return 1
        target = ports[-1]
        print(f"--port verilmedi, en son listelenen kullaniliyor: {target}")
    else:
        target = args.port

    if args.baud_scan:
        for b in (115200, 460800, 921600):
            sniff(target, b, args.seconds)
    else:
        sniff(target, args.baud, args.seconds)

    return 0


if __name__ == "__main__":
    sys.exit(main())
