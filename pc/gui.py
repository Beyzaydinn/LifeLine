#!/usr/bin/env python3
"""
First Aid Assistant — PC graphical interface.
No physical button needed: Start/Stop recording from this window.

Usage:
  python gui.py
  python gui.py --port COM7
"""

from __future__ import annotations

import argparse
import logging
import threading
import time
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox

from serial.tools import list_ports

from serial_bridge import SerialBridge, Vitals
import stt
import llm
import tts

logging.basicConfig(level=logging.INFO)
log = logging.getLogger("gui")

STATE_NAMES = {
    0: "IDLE (ready)",
    1: "RECORDING",
    2: "PROCESSING",
    3: "SPEAKING",
    4: "ERROR",
}


class FirstAidApp(tk.Tk):
    def __init__(self, port: str | None) -> None:
        super().__init__()
        self.title("First Aid Assistant — Offline")
        self.geometry("720x560")
        self.resizable(True, True)

        self.bridge: SerialBridge | None = None
        self._busy = False
        # Last valid "Read vitals" measurement, passed to the LLM. The finger is
        # usually off the sensor while speaking, so the real-time stream during
        # recording is invalid — we reuse this measured value instead.
        self._measured_vitals = Vitals()

        top = ttk.Frame(self, padding=8)
        top.pack(fill=tk.X)

        ttk.Label(top, text="COM port (USB-OTG):").pack(side=tk.LEFT)
        self.port_var = tk.StringVar(value=port or "")
        ports = [p.device for p in list_ports.comports()]
        self.port_combo = ttk.Combobox(top, textvariable=self.port_var, values=ports, width=12)
        self.port_combo.pack(side=tk.LEFT, padx=6)
        if ports and not self.port_var.get():
            self.port_var.set(ports[-1])

        ttk.Button(top, text="Connect", command=self._connect).pack(side=tk.LEFT, padx=4)
        ttk.Button(top, text="Refresh ports", command=self._refresh_ports).pack(side=tk.LEFT)

        self.status_var = tk.StringVar(value="Not connected")
        ttk.Label(self, textvariable=self.status_var, font=("", 10, "bold")).pack(pady=4)

        vitals_fr = ttk.LabelFrame(self, text="MAX30102 — place finger on sensor", padding=8)
        vitals_fr.pack(fill=tk.X, padx=8, pady=4)
        self.vitals_var = tk.StringVar(value="HR: —   SpO2: —")
        ttk.Label(vitals_fr, textvariable=self.vitals_var).pack(side=tk.LEFT)
        self.btn_vitals = ttk.Button(vitals_fr, text="Read vitals", command=self._measure_vitals)
        self.btn_vitals.pack(side=tk.RIGHT)
        self.vitals_progress = ttk.Progressbar(vitals_fr, mode="determinate", length=160)
        self.vitals_progress.pack(side=tk.RIGHT, padx=8)

        ctrl = ttk.LabelFrame(self, text="Voice (INMP441) — describe emergency in English", padding=8)
        ctrl.pack(fill=tk.X, padx=8, pady=4)

        btn_row = ttk.Frame(ctrl)
        btn_row.pack(fill=tk.X)
        self.btn_start = ttk.Button(btn_row, text="Start recording", command=self._start_record, state=tk.DISABLED)
        self.btn_start.pack(side=tk.LEFT, padx=4)
        self.btn_stop = ttk.Button(btn_row, text="Stop and analyze", command=self._stop_analyze, state=tk.DISABLED)
        self.btn_stop.pack(side=tk.LEFT, padx=4)

        ttk.Label(ctrl, text="NeoPixel on ESP32 shows: green=idle, red=recording, purple=thinking, green=speaking").pack(anchor=tk.W, pady=4)

        out_fr = ttk.LabelFrame(self, text="Results", padding=8)
        out_fr.pack(fill=tk.BOTH, expand=True, padx=8, pady=4)
        self.output = scrolledtext.ScrolledText(out_fr, height=16, wrap=tk.WORD, font=("Segoe UI", 10))
        self.output.pack(fill=tk.BOTH, expand=True)

        ttk.Label(
            self,
            text="Academic demo only — not a medical device. Call emergency services in real emergencies.",
            foreground="gray",
        ).pack(pady=4)

        # Pre-load the speech model (Whisper) in the background so the first real
        # transcribe does not wait ~2 s for loading. GPU if available, else CPU.
        self._prewarm_stt()

    def _prewarm_stt(self) -> None:
        self._log("Loading speech model in background...")

        def work() -> None:
            t0 = time.time()
            stt.prewarm()
            dt = time.time() - t0
            self.after(0, lambda: self._log(f"Speech model ready ({dt:.1f}s)."))

        threading.Thread(target=work, daemon=True).start()

    def _refresh_ports(self) -> None:
        ports = [p.device for p in list_ports.comports()]
        self.port_combo["values"] = ports
        if ports:
            self.port_var.set(ports[-1])

    def _log(self, text: str) -> None:
        self.output.insert(tk.END, text + "\n")
        self.output.see(tk.END)

    def _on_status(self, state: int, _err: int) -> None:
        name = STATE_NAMES.get(state, f"state {state}")
        self.after(0, lambda: self.status_var.set(f"ESP32: {name}"))

    def _connect(self) -> None:
        port = self.port_var.get().strip()
        if not port:
            messagebox.showerror("Error", "Select a COM port")
            return
        # Close any existing connection first (prevents a port clash on double Connect).
        if self.bridge is not None:
            try:
                self.bridge.close()
            except Exception:
                pass
            self.bridge = None
            time.sleep(0.3)
        try:
            self.bridge = SerialBridge(port=port)
            self.bridge.set_status_callback(self._on_status)
            self.bridge.open()
            self.bridge.start_reader()
            self.bridge.wait_crypto_cap(8)
            self.status_var.set("ESP32: connected")
            self.btn_start.config(state=tk.NORMAL)
            self._log(f"Connected to {port}")
            self.vitals_var.set("Press 'Read vitals' and place fingertip on MAX30102")
        except Exception as e:
            messagebox.showerror("Connect failed", str(e))

    def _measure_vitals(self) -> None:
        if not self.bridge or self._busy:
            return
        self._busy = True  # block recording start during the ~15 s measurement
        self.btn_vitals.config(state=tk.DISABLED)
        self.vitals_progress["value"] = 0
        self._log("Measuring vitals — keep fingertip still on the MAX30102...")

        def on_update(v, elapsed: float, duration: float) -> None:
            pct = max(0.0, min(100.0, 100.0 * elapsed / duration)) if duration > 0 else 0.0
            if v.valid and v.heart_rate > 0:
                txt = f"Measuring... HR: {v.heart_rate} BPM   SpO2: ~{v.spo2}% (estimate)"
            else:
                txt = "Measuring... keep fingertip still"
            self.after(0, lambda: self.vitals_var.set(txt))
            self.after(0, lambda: self.vitals_progress.config(value=pct))

        def work() -> None:
            try:
                v = self.bridge.measure_vitals(on_update=on_update)
                if v.valid and v.heart_rate > 0:
                    self._measured_vitals = v  # remember for the LLM's assessment
                    final = f"HR: {v.heart_rate} BPM   SpO2: ~{v.spo2}% (estimate)"
                else:
                    final = "Could not get a reliable reading. Place fingertip gently and keep still."
                self.after(0, lambda: self.vitals_var.set(final))
                self.after(0, lambda: self._log(final))
            except Exception as e:
                # Convert to string now: `e` is unbound once the except block
                # exits (Python 3), so a deferred lambda must capture the text.
                err_msg = f"{type(e).__name__}: {e}"
                import traceback
                traceback.print_exc()
                self.after(0, lambda msg=err_msg: messagebox.showerror("Vitals error", msg))
                self.after(0, lambda msg=err_msg: self._log(f"ERROR: {msg}"))
            finally:
                self._busy = False
                self.after(0, lambda: self.vitals_progress.config(value=0))
                self.after(0, lambda: self.btn_vitals.config(state=tk.NORMAL))

        threading.Thread(target=work, daemon=True).start()

    def _start_record(self) -> None:
        if not self.bridge or self._busy:
            return
        self.bridge.start_recording()
        self.btn_start.config(state=tk.DISABLED)
        self.btn_stop.config(state=tk.NORMAL)
        self._log("Recording... speak now.")

    def _stop_analyze(self) -> None:
        if not self.bridge or self._busy:
            return
        self._busy = True
        self.btn_stop.config(state=tk.DISABLED)

        def work() -> None:
            try:
                pcm = self.bridge.stop_recording()
                # Prefer the last "Read vitals" measurement: the finger is usually off
                # the sensor while speaking, so the recording-time stream is invalid.
                vitals = self._measured_vitals if self._measured_vitals.valid else self.bridge.last_vitals()
                self.after(0, lambda: self._log("Transcribing (Whisper)..."))
                text = stt.transcribe(pcm) or "I need first aid help"
                self.after(0, lambda: self._log(f"You said: {text}"))
                self.after(0, lambda: self._log("Generating reply (Llama)..."))
                reply = llm.generate(text, vitals)
                self.after(0, lambda: self._log(f"\nAssistant:\n{reply}\n"))
                self.after(0, lambda: self._log("Playing on speaker (Piper)..."))
                for chunk in tts.synthesize_stream(reply):
                    self.bridge.send_audio_down(chunk)
                self.bridge.send_playback_end()
                self.after(0, lambda: self._log("Done.\n"))
            except Exception as e:
                # In Python 3, `e` is auto-deleted when the `except as e` block exits,
                # so a deferred lambda would raise NameError. Convert the error to a
                # string immediately and capture that in the closure.
                err_msg = f"{type(e).__name__}: {e}"
                import traceback
                traceback.print_exc()  # write the full stack to the console
                self.after(0, lambda msg=err_msg: messagebox.showerror("Error", msg))
                self.after(0, lambda msg=err_msg: self._log(f"ERROR: {msg}"))
            finally:
                self._busy = False
                self.after(0, lambda: self.btn_start.config(state=tk.NORMAL))
                self.after(0, lambda: self.btn_stop.config(state=tk.DISABLED))

        threading.Thread(target=work, daemon=True).start()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="COM port")
    args = parser.parse_args()
    app = FirstAidApp(port=args.port)
    app.mainloop()


if __name__ == "__main__":
    main()
