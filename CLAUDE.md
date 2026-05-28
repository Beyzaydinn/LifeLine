# LifeLine — CEN322 Offline Voice First-Aid Assistant

Üniversite IoT dersi (CEN322) final projesi. Tamamen offline çalışan,
sesle kontrol edilen ilk yardım asistanı. İnternet/API yok.

## İki makineli mimari

İş iki cihaza bölünmüş; ikisi USB-UART üzerinden şifreli binary protokolle konuşur.

- **ESP32-S3-DevKitM-1-N8** (`firmware/`): mikrofon yakalama (INMP441 I2S),
  hoparlör çıkışı (MAX98357A I2S), MAX30102 sensör (I2C), WS2812B NeoPixel
  durum LED'i (RMT). ESP-IDF v5.5.3 + FreeRTOS.
- **PC uygulaması** (`pc/`): Faster-Whisper STT + Ollama Llama 3.2 3B +
  Piper TTS. Tkinter GUI (`gui.py`). Tüm AI lokal.

Veri yolu: PC `gui.py` → mikrofon kaydı için ESP'ye START/STOP → ESP ham PCM'i
chunk'lar halinde yollar → Whisper transkript → Llama yanıt → Piper TTS →
ESP'ye AES'li audio chunk'lar → MAX98357A hoparlör.

## İki-düzlemli I/O (önemli)

ESP'de iki ayrı USB bağlantısı, ayrı görevler için:

- **DATA düzlemi**: UART0 (GPIO 43 TX / 44 RX, 921600 baud) → on-board
  CP210x/CH9102 USB-UART köprüsü → PC'de bir COM port (ör. COM12). SADECE
  framed binary protokol (AES'li audio/sensor/control). Console/log YOK.
- **LOG düzlemi**: USB-Serial-JTAG (native USB konnektörü) → ayrı COM port
  (ör. COM13). ESP_LOGx çıktısı ve panic trace'leri. `idf.py -p COMxx monitor`.

İki düzlem tel ve sürücü paylaşmaz; log trafiği binary stream'i bozamaz.
(Ayrıntı: `firmware/sdkconfig.defaults` üst yorumu.)

## Build / Flash / Monitor (ESP)

ESP-IDF v5.5.3 ortamı aktifken, `firmware/` dizininden:

```
idf.py build flash          # derle + flash (DATA portu, ör. COM12)
idf.py -p COM13 monitor     # logları izle (LOG portu, USB-JTAG)
```

ESP-IDF kurulumu: `C:\esp\v5.5.3\esp-idf`.

## Çalıştırma / Test (PC)

Python venv: `pc/.venv/`. Her zaman venv python'unu kullan:

```
cd pc
.\.venv\Scripts\python.exe gui.py              # ana GUI uygulamasi
.\.venv\Scripts\python.exe _test_tts_calm.py   # TTS smoke (WAV uretir)
.\.venv\Scripts\python.exe _test_esp_playback.py  # uctan uca ESP playback timing
.\.venv\Scripts\python.exe diag_raw_uart.py    # ham UART byte sniffer
```

Bağımlılıklar: `pc/requirements.txt` (pyserial, pycryptodomex, faster-whisper,
piper-tts). Modeller `pc/models/` (gitignore'da): Piper `.onnx` + Whisper
`faster-whisper-small.en/`. STT tam offline: `stt.py` `HF_HUB_OFFLINE` +
`local_files_only` ile network'e çıkmaz, modeli yerelden yükler (yoksa HF
cache'e düşer).

## Ses formatı (kritik detay)

- Mikrofon & hoparlör: **16 kHz mono int16**. 22050 Hz denendi, MAX98357A/ESP32
  I2S driver temiz türetemedi.
- ESP I2S TX (`audio_output.c`): **STEREO 32-bit slot**, her mono sample L+R'a
  duplicate edilip `(int32)<<16` MSB-aligned yazılır. `audio_input.c` ile
  simetrik. ESP-IDF v5 driver data_bit_width < slot_bit_width durumunda
  otomatik padding YAPMAZ — bunu yapmazsan chipmunk (2× hız + 1 oktav) olur.
- PC `serial_bridge.send_audio_down`: audio'yu **real-time hızda** yollar
  (16 ms/chunk). UART burst hızında yollarsa ESP playback queue (depth 8)
  taşar, chunk drop olur.
- TTS tempo: `pc/tts.py` üstünde `LENGTH_SCALE` (1.25) ve `SENTENCE_SILENCE_S`
  (0.45) sabitleri. Piper SynthesisConfig'e geçer.

## Şifreleme

AES-256-CBC + PKCS#7, ESP'de mbedtls HW hızlandırmalı (`crypto_aes.c`),
PC'de pycryptodomex (`crypto_aes.py`). **Bu şifreleme kalacak — ders
yönergesi gereği.** Demo key/IV, production değil.

## Kurallar / kısıtlar

- ESP-IDF kullanılıyor (Arduino değil), hoca onayladı.
- AES şifreleme kaldırılmayacak (ders gereksinimi).
- API key yok, network kodu yok — tamamen offline.
- Hocanın ödev yönergesi: `docs/instructions/homework_instructions.md` +
  PDF rapor şablonları.

## Dizin yapısı

```
firmware/main/    ESP-IDF kaynak (main.c FSM + task'ler, audio_*, sensor, crypto, uart_link, protocol, led_strip_ctrl)
firmware/sdkconfig.defaults   iki-düzlemli I/O config + AES HW + FreeRTOS
pc/               PC tarafı (gui, stt, llm, tts, serial_bridge, crypto_aes, protocol)
pc/_test_*.py     diagnostic/test scriptleri
docs/             raporlar (.md), instructions/, superpowers/specs/ (tasarım dökümleri)
wokwi/            simülasyon (diagram.json, sketch.ino) — pin'leri firmware ile UYUMSUZ olabilir
```

## Notlar

- `firmware/main/usb_cdc.c/h`: eski USB-OTG CDC yaklaşımından kalma, artık
  UART kullanılıyor (Windows TinyUSB CDC uyumsuzluğu nedeniyle terk edildi).
- MAX30102 BPM/SpO2 şu an sahte değer üretiyor (modulo aritmetiği), gerçek
  PPG analizi henüz yok.
