# CEN322 — Offline Voice First-Aid Assistant

**Your hardware only** — no extra push button required.

| Part | Role |
|------|------|
| ESP32-S3-DevKitM-1-N8 | Main controller |
| INMP441 | Microphone (I2S) |
| MAX98357A + 4Ω speaker | Voice output (I2S) |
| MAX30102 | Heart rate / SpO2 (I2C) |
| WS2812B ×8 ring | Status LEDs |
| PC | USB-OTG — AI + **graphical interface** |

## Wiring

| Module | GPIO | Power |
|--------|------|-------|
| INMP441 SD / SCK / WS | 2 / 41 / 42, L/R→GND | 3.3 V |
| MAX98357 DIN / BCLK / LRC / SD | 7 / 5 / 6 / 10 | 5 V VBUS |
| MAX30102 SDA / SCL / INT | 8 / 9 / 4 | 3.3 V |
| NeoPixel DIN | 38 (+330 Ω) | 5 V VBUS |

- **UART USB:** `idf.py flash monitor`
- **OTG USB:** PC app `python gui.py`

## 1. Flash firmware

```powershell
cd firmware
idf.py set-target esp32s3
idf.py -p COMx flash monitor
```

## 2. Run PC app (GUI)

```powershell
ollama pull llama3.2:3b
cd pc
python -m venv .venv
.\.venv\Scripts\activate
pip install -r requirements.txt
python gui.py
```

1. **Connect** (OTG COM port)
2. **Read vitals** — finger on MAX30102
3. **Start recording** → speak in English
4. **Stop and analyze** → hear reply on speaker

CLI: `python main.py --port COMy` then `s` start, `e` end.

## Project layout

```
firmware/     ESP-IDF
pc/           gui.py, main.py, stt, llm, tts
wokwi/        diagram + link
```

## Wokwi

Sadece `wokwi/diagram.json` → Wokwi projesine yapıştır (tam pin şeması + LED halka).  
Simülasyon kodu için Wokwi’de ayrı `sketch.ino` ekleyebilirsin; gerçek sistem `firmware/` + `pc/gui.py`.

## LMS

- `firmware/` (ESP-IDF, not .ino)
- `wokwi/project_link.txt`
- `README.md`
