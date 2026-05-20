#!/usr/bin/env python3
"""CLI entry — for GUI run: python gui.py"""

import sys

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("--gui", "-g"):
        sys.argv.pop(1)
        from gui import main

        raise SystemExit(main())

    import argparse
    import logging
    import time

    from crypto_aes import self_test as crypto_self_test
    from serial_bridge import SerialBridge
    import stt
    import llm
    import tts

    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")

    parser = argparse.ArgumentParser(description="First Aid Assistant (CLI)")
    parser.add_argument("--port", help="COM port")
    parser.add_argument("--test-crypto", action="store_true")
    parser.add_argument("--list-ports", action="store_true")
    parser.add_argument("--gui", "-g", action="store_true", help="Open graphical interface")
    args = parser.parse_args()

    if args.gui:
        from gui import main as gui_main

        raise SystemExit(gui_main())

    if args.list_ports:
        SerialBridge.list_ports()
        raise SystemExit(0)

    if args.test_crypto:
        print("AES:", "OK" if crypto_self_test() else "FAIL")
        raise SystemExit(0)

    bridge = SerialBridge(port=args.port)
    bridge.open()
    bridge.start_reader()
    bridge.wait_crypto_cap()
    print("Connected. Commands: [s]tart record, [e]nd and analyze, [v]itals, [q]uit")

    try:
        while True:
            cmd = input("> ").strip().lower()
            if cmd in ("q", "quit"):
                break
            if cmd in ("v", "vitals"):
                v = bridge.read_sensor()
                print(f"HR={v.heart_rate} SpO2={v.spo2}% valid={v.valid}")
            elif cmd in ("s", "start"):
                bridge.start_recording()
                print("Recording... type 'e' when done")
            elif cmd in ("e", "end"):
                pcm = bridge.stop_recording()
                text = stt.transcribe(pcm) or "help"
                print("You:", text)
                reply = llm.generate(text, bridge.last_vitals())
                print("Assistant:", reply)
                for chunk in tts.synthesize_stream(reply):
                    bridge.send_audio_down(chunk)
                    time.sleep(0.016)
                bridge.send_playback_end()
            else:
                print("Use s, e, v, or q")
    finally:
        bridge.close()
