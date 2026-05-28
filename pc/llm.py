"""LLM via local Ollama (Llama 3.2 3B)."""

from __future__ import annotations

import json
import logging
from pathlib import Path
from typing import TYPE_CHECKING

import requests

if TYPE_CHECKING:
    from serial_bridge import Vitals

log = logging.getLogger(__name__)

OLLAMA_URL = "http://127.0.0.1:11434/api/generate"
MODEL = "llama3.2:3b"


def load_system_prompt() -> str:
    path = Path(__file__).parent / "system_prompt.txt"
    return path.read_text(encoding="utf-8").strip()


def generate(user_text: str, vitals: "Vitals") -> str:
    system = load_system_prompt()
    sensor_line = ""
    if vitals.valid:
        sensor_line = f"\nVitals from sensor: heart rate ~{vitals.heart_rate} BPM, SpO2 ~{vitals.spo2}%."
    else:
        sensor_line = "\nVitals: sensor not valid (finger may not be on MAX30102)."

    prompt = f"{system}{sensor_line}\n\nUser describes the emergency:\n{user_text}\n\nAssistant response:"

    payload = {
        "model": MODEL,
        "prompt": prompt,
        "stream": False,
        "options": {
            "temperature": 0.3,
            "num_predict": 200,
        },
    }

    log.info("Calling Ollama (%s)...", MODEL)
    try:
        r = requests.post(OLLAMA_URL, json=payload, timeout=120)
        r.raise_for_status()
        data = r.json()
        reply = data.get("response", "").strip()
    except requests.RequestException as e:
        log.error("Ollama error: %s", e)
        reply = (
            "Stay calm. Ensure the area is safe and call emergency services immediately. "
            "Check if the person is breathing and responsive."
        )

    log.info("LLM reply: %s", reply[:200])
    return reply
