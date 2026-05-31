# Proje Yapılacaklar — Kod, Dokümantasyon, Teslimat

> **Amaç:** `IOT_final.docx` dışındaki tüm eksikler/işler. Kaynak: firmware + PC +
> dokümantasyon/teslimatları dosya-dosya tarayan **adversarial defect-hunt
> workflow'u** (34 ajan). Her yüksek/orta bulgu, kodu yeniden okuyup **çürütmeye
> çalışan** ayrı bir şüpheci ajanla doğrulandı. Aşağıda **yalnızca gerçek
> (is_real=true)** bulgular var; önem dereceleri doğrulama sonrası **düzeltilmiş**
> değerlerdir (şişirilmiş ilk değerler değil). En sonda "kontrol edildi, sorun
> değil" listesi de var (güven için).
>
> **Sadece raporla ilgili düzeltmeler için bkz. `RAPOR_DUZELTMELERI.md`.**
>
> ⏰ **Teslim: 2026-06-01 23:59 (yarın). Bugün 2026-05-31.** Önce "🔴 Teslim öncesi
> ZORUNLU" bölümünü bitir.

---

## Öncelik tablosu

| # | İş | Konum | Önem | Kategori |
|---|----|-------|------|----------|
| **T1** | docx'i `CEN322_Project_...docx` olarak yeniden adlandır | `IOT_final.docx` | 🔴 Zorunlu | Teslimat |
| **T2** | `project_link.txt`'e gerçek Wokwi URL'sini yaz | `wokwi/project_link.txt` | 🔴 Zorunlu | Teslimat |
| **T3** | `numpy`'ı `requirements.txt`'e ekle | `pc/requirements.txt` | 🟠 Yüksek | Tekrar-üretilebilirlik |
| **T4** | README "USB-OTG" → "USB-UART (CP210x)" düzelt | `README.md:12,24,45` | 🟠 Yüksek | Doküman |
| **D1** | Parmak algılama eşiği — vitals "invalid" riski (donanımda test et) | `sensor.c:96-102` | 🟠 Demo-risk | Firmware |
| **D2** | Ollama hatası: sessiz fallback + 120 s timeout | `pc/llm.py:50-63` | 🟠 Demo-risk | PC |
| **D3** | GUI kapanınca seri port kapatılmıyor | `pc/gui.py` | 🟠 Demo-risk | PC |
| **D4** | Crypto-cap handshake + COM-port seçimi enforce edilmiyor | `serial_bridge.py`, `gui.py` | 🟡 Orta | PC |
| **C1** | Ölü kod `usb_cdc.c/.h` sil + `esp_tinyusb` bağımlılığını kaldır | `firmware/main/` | 🟡 Temizlik | Firmware |
| **C2** | Türkçe yorumları İngilizce'ye çevir (English-only kuralı) | çeşitli | 🟡 Temizlik | Konvansiyon |
| **C3** | `last_recording.wav` git'ten çıkar + `.gitignore` | `pc/` | 🟢 Düşük | Repo hijyeni |
| **C4** | Mic gain yorumu yanlış (~8x değil ~32x) + tunable macro | `audio_input.c:76` | 🟢 Düşük | Firmware |
| **O1–O7** | Opsiyonel sağlamlaştırmalar (aşağıda) | çeşitli | 🟢 Opsiyonel | Karışık |

---

## 🔴 Teslim öncesi ZORUNLU (yarın deadline)

### T1 — docx dosya adı yanlış
- **Sorun:** Hoca `CEN322_Project_YourName_YourSurname.docx` istiyor
  (`homework_instructions.md:10`). Dosya şu an `IOT_final.docx`.
- **Yap:**
  - Word'ü kapat (açık → `~$T_final.docx` lock dosyası var).
  - **ASCII** ad ver (Türkçe karakter yok): ör.
    `CEN322_Project_Beyza_Aydin_Ahmet_Bera_Celik.docx` veya hocanın istediği
    tek-ad formatına göre.
  - `~$*.docx` ve `*.docx` lock dosyalarını teslim etme.

### T2 — Wokwi `project_link.txt` placeholder
- **Sorun:** Dosya hâlâ `https://wokwi.com/projects/YOUR_PROJECT_ID`. Rapor ise
  gerçek linki (`.../464462040095424513`) veriyor → tutarsız ve teslimat eksik.
- **Yap:**
  1. `wokwi/diagram.json` + `wokwi/sketch.ino`'yu Wokwi'ye yükle, **Save**.
  2. Gerçek URL'yi `project_link.txt`'e yapıştır (placeholder'ı değiştir).
  3. **Projenin public/erişilebilir olduğunu doğrula** (rapordaki ID ile aynı mı?).
- İlişki: `RAPOR_DUZELTMELERI.md` #2 (rapor bu dosyaya atıf yapıyor).

### T3 — `numpy` `requirements.txt`'te eksik
- **Sorun:** `stt.py:75` ve `tts.py:50` doğrudan `import numpy as np` yapıyor ama
  `requirements.txt`'te numpy yok. Şu an sadece faster-whisper/piper transitively
  çektiği için çalışıyor (venv'de 2.4.6). Temiz bir kurulumda `ModuleNotFoundError`
  riski → demo makinesi dışında kurulum yapan grader'da kırılabilir.
- **Yap:** `pc/requirements.txt`'e ekle:
  ```
  numpy>=1.24
  ```
  (İstersen kanıtlanmış sürümle: `numpy>=2,<3`.)
- Not: `onnxruntime`, `av`, `ctranslate2` doğrudan import edilmiyor (gerçek
  transitive bağımlılıklar) → onları eklemeye gerek yok.

### T4 — README "USB-OTG" terminolojisi eskimiş
- **Sorun:** README satır 12 (`PC | USB-OTG`), 24 (`OTG USB: ...`), 45
  (`Connect (OTG COM port)`) **terk edilmiş** USB-OTG/CDC yaklaşımını anlatıyor.
  Gerçek veri yolu UART0 / CP210x-CH9102 köprüsü. Grader olmayan bir OTG portu
  arar.
- **Yap:** "USB-OTG / OTG USB / OTG COM port" → **"USB-UART (CP210x/CH9102 bridge,
  921600 baud)"**. DATA portu (CP210x, `gui.py`) ile LOG portunu (USB-Serial-JTAG,
  `idf.py monitor`) ayır — `sdkconfig.defaults` başlığındaki gibi.
- Bonus (kolay): README satır 39-43/50 `python gui.py` yerine CLAUDE.md
  konvansiyonu `.\.venv\Scripts\python.exe gui.py` kullanabilir (düşük öncelik).

---

## 🟠 Demo günü güvenliği (şiddetle önerilir)

### D1 — Parmak algılama eşiği vitals'ı "invalid" bırakabilir *(tek yüksek kod riski)*
- **Konum:** `sensor.c:96-102` (`FINGER_PRESENT_FLOOR=50000`) + `ppg.c:9`.
- **Sorun:** `drain_fifo()` **tek bir** ham IR örneği 50000'in altına düşünce
  **tüm ring buffer'ı sıfırlıyor** (`s_widx=0; s_count=0`). Geçerli okuma için
  ~5-6 sn kesintisiz temas gerekiyor (≥2 s veri + 1 s startup skip + ≥4 IBI).
  Soğuk/gevşek/kayan parmakta (LED akımı yalnız ~16 mA) tek anlık dip bile
  sayacı sürekli sıfırlayıp **kalıcı invalid** üretebilir. Doğrulama bunu
  **gerçek + yüksek** olarak teyit etti (donanımda ne sıklıkta tetiklendiği
  ölçülmedi).
- **Önemli bağlam:** Git geçmişi ("Tune PPG peak detection on real hardware") ve
  memory ("~74 vs manuel 76 BPM") **sizin donanımınızda çalıştığını** gösteriyor.
  Yani muhtemelen demoda sorun çıkmaz — ama **kırılgan**.
- **Yap (öncelik sırasıyla, en güvenli demo için):**
  1. **Demo öncesi ölçüm:** Gerçek parmakla ham IR DC'yi logla; 50000'in ne kadar
     üstünde/altında olduğunu gör.
  2. Tek dip'te resetlemek yerine **N ardışık (örn. 10 = 0.2 s) altında-eşik**
     örnek iste; izole dip'leri tolere et.
  3. Gerekirse `FINGER_PRESENT_FLOOR`'u ~20000–30000'e indir **ya da** LED akımını
     (`LED1_PA/LED2_PA` 0x50 → ~0x7F) artır.
  4. En temizi: sampler'da history'yi yok etme; geçerliliğe `ppg.c`'nin kendi
     `ir_dc` ortalama kapısı karar versin.
- **Düşük çaba/yüksek değer:** ESP loguna `cv` ve `ir_dc` değerlerini ekleyip
  demo öncesi izle.

### D2 — Ollama hatası fark edilmeden "sahte" cevap konuşuyor
- **Konum:** `pc/llm.py:50-63`.
- **Sorun:** Ollama kapalı/model yok/timeout olursa kod **sabit 2 cümlelik
  fallback** döndürüyor ve bu, gerçek cevapmış gibi loglanıp Piper ile
  **seslendiriliyor** — operatör fark etmez. Ayrıca **120 s timeout** çok uzun
  (donmuş Ollama UI'yı 2 dk kilitler). Fallback vitals'ı da yok sayıyor.
- **Yap:**
  - Fallback'i GUI durum/çıktısına **görünür** yap ("Ollama unavailable — using
    fallback") — `log.error` zaten var, ekrana da yaz.
  - Timeout'u **30-45 s**'ye indir (veya configurable).
  - Demo öncesi `ollama list` ile `llama3.2:3b`'nin pull'lu olduğunu doğrula.

### D3 — GUI kapanınca seri port kapatılmıyor
- **Konum:** `pc/gui.py` (WM_DELETE_WINDOW handler yok), `serial_bridge.py:122`.
- **Sorun:** Pencere kapatılınca `bridge.close()` çağrılmıyor; CP210x/CH9102 portu
  Windows'ta kısa süre "in use" kalıyor → **tekrar açışta "Access is denied"**.
  Demoda çok yapılan "kapat-aç" akışında ara sıra patlar.
- **Yap:** `__init__`'e ekle:
  ```python
  self.protocol('WM_DELETE_WINDOW', self._on_close)
  # ...
  def _on_close(self):
      if self.bridge:
          self.bridge.close()
      self.destroy()
  ```

### D4 — Crypto-cap handshake ve COM-port seçimi enforce edilmiyor
- **Konum:** `serial_bridge.py:126-127` (`wait_crypto_cap`), `gui.py:149-151`,
  `serial_bridge.py:84-96` (`_auto_port`), `gui.py:62,121`.
- **Sorun:** `wait_crypto_cap` `Event.wait()` dönüşünü **yok sayıyor**; ESP hiç
  `CRYPTO_CAP` göndermese bile (yanlış port / LOG portu seçilmiş / firmware
  uyumsuz) GUI "connected" deyip Start'ı açıyor. Ayrıca GUI portu **`ports[-1]`**
  (en yüksek COM) varsayıyor — yanlışlıkla JTAG/log portu seçilebilir, o da
  protokol taşımaz → sessiz başarısızlık.
- **Yap:**
  - `wait_crypto_cap` timeout'ta **raise/uyarı** versin; `_connect` yalnızca
    başarıda Start'ı açsın, aksi halde messagebox göstersin.
  - Port seçimini CP210x/CH9102 açıklamasıyla eşle, "JTAG"'i hariç tut; combobox'ı
    `ports[-1]`'e default'lama.

---

## 🟡 Kod temizliği (puan kaybı yok ama repo/teslim kalitesi)

### C1 — Ölü kod + gereksiz bağımlılık
- **`usb_cdc.c` / `usb_cdc.h`:** Derlenmiyor (CMakeLists SRCS'te yok), terk edilmiş
  TinyUSB CDC yolu. **Sil** (gerekçe zaten `uart_link.h` başlığı + CLAUDE.md'de).
- **`idf_component.yml:3` `espressif/esp_tinyusb ^1.4.0`:** Tek tüketicisi ölü
  `usb_cdc.c`. **Kaldır** ve `idf.py reconfigure` çalıştır → build hızlanır.

### C2 — Türkçe yorumlar (English-only kuralı ihlali)
- **Sorun:** Teslim edilen firmware/ ve `.ino` dahil bazı dosyalarda Türkçe yorum
  var; CLAUDE.md "tüm proje artifact'ları İngilizce" diyor, grader görebilir.
- **Konumlar (doğrulandı):**
  - `firmware/main/audio_output.c:38,50` (ve 15-21 bloğu)
  - `firmware/main/config.h:74-79`
  - `firmware/main/led_strip_ctrl.c:12-13`
  - `pc/tts.py:15-21,31-40` (docstring + sat. 34)
  - `pc/diag_raw_uart.py:1,114`
  - `pc/_test_esp_playback.py:1-17,102,126,130,132`
  - `.gitignore:13`
- **Yap:** Bu yorumları İngilizce'ye çevir. (Kod/tanımlayıcılar zaten İngilizce —
  sadece yorumlar.) **Not:** `docs/esp32s3_donanim_raporu.md` ve
  `docs/proje_plani_ve_mimari_v2.md` tamamen Türkçe — bunlar **iç notlar**, LMS'e
  teslim **edilmiyor**, dolayısıyla sorun değil (teslim setine ekleme).

### C3 — `last_recording.wav` repo'da izleniyor
- **Konum:** `pc/last_recording.wav` (tracked, ~200 KB, her kayıtta yeniden üretilir).
- **Yap:**
  ```
  # .gitignore'a:
  pc/last_recording.wav
  # ve:
  git rm --cached pc/last_recording.wav
  ```

### C4 — Mic gain yorumu yanlış + tunable değil
- **Konum:** `audio_input.c:76-98`.
- **Sorun:** Yorum "~8x" diyor ama `>>11` aslında **~32x** (unity = `>>16`).
  Hard-clamp var, AGC yok → yüksek sesli/yakın konuşmada clip olabilir. Sabit
  literal olduğu için demoda reflash olmadan ayarlanamaz. *(Agresif gain bilinçli
  bir tercih — INMP441 düşük seviyeli; yani kötü değil, ama yorum yanlış.)*
- **Yap:** Yorumu düzelt (`>>11 ≈ 32x vs unity >>16`). Opsiyonel:
  `#define MIC_GAIN_SHIFT 11` config.h'a taşı; demoda gerekirse 13/14 dene.

---

## 🟢 Opsiyonel sağlamlaştırmalar (gerçek ama düşük; istersen)

- **O1 — SpO₂ kalibre değil + clamp hatayı gizliyor** (`ppg.c:178-184`, 🟡 orta):
  `104−17R` kalibre değil; sonuç [70,100]'e clamp'lendiği için saçma R bile
  "sağlıklı" görünür. **Aksiyon:** Raporda "uncalibrated estimate" de (zaten
  `RAPOR_DUZELTMELERI.md` #1'de). Opsiyonel kod: R'yi makul aralıkta (0.4–1.2)
  gate'le, dışındaysa `spo2=0/invalid` ver.
- **O2 — TTS gerçek streaming değil** (`tts.py:54-92`): "stream" adına rağmen tüm
  ses sentezlenip resample edilince yield ediliyor → ilk sözcükten önce birkaç sn
  ölü hava. Çalışıyor, sadece gecikme. İstersen chunk-bazlı resample+yield.
- **O3 — `stop_recording` sabit 1.5 s sleep** (`serial_bridge.py:198`): magic
  sabit; her durdurmaya 1.5 s ekliyor, ağır yükte kuyruk hâlâ kesilebilir.
  İdeali ESP'den "kayıt bitti" marker frame'i bekleyip event'le sonlandırmak.
- **O4 — Whisper GPU→CPU fallback yalnız yükleme anında**, çalışma anında CUDA OOM
  fallback'i yok (`stt.py:140-157`). VRAM bütçesi ölçülü olduğu için düşük risk.
  Opsiyonel: `transcribe`'ı try/except'e al, OOM'da CPU'ya düş.
- **O5 — Frame parser CRC uzunluk alanını kapsamıyor + bozuk frame sonrası
  yeniden-SYNC taraması yok** (`protocol.c`): kablolu UART'ta düşük risk; bounded
  (hang yok). İstersen CRC'yi `buf[2..]` üzerinden hesapla (iki tarafta birlikte).
- **O6 — Crypto cleartext passthrough footgun** (`crypto_aes.c:166-173`): flag
  bit0=0 olan blob şifresiz geçiyor. PC her zaman şifrelediği + offline + demo
  anahtarı olduğu için düşük. İstersen `protocol_type_encrypted()` tipleri için
  cleartext'i reddet.
- **O7 — Latent buffer/eşzamanlılık nitelikleri** (hepsi düşük): `audio_in_read_chunk`
  `max_samples` clamp'i yok (bugün güvenli); CLI `main.py:70` çift-pacing
  (`send_audio_down` zaten pace ediyor → CLI testte 2x yavaş); `read_sensor` 0.3 s
  sleep'le stale vitals riski. Bunlar demo yolunu bozmuyor.

---

## ✅ Kontrol edildi — SORUN DEĞİL (şüpheci doğrulama ile çürütüldü)

Bunlar tarandı ve **gerçek sorun olmadığı** kanıtlandı; güvenle es geçebilirsin:

- **I2S çapraz-task race (start/stop vs write):** ESP-IDF I2S API'leri **thread-safe**
  (driver kendi mutex'ini tutar). Sorun değil.
- **`valid=0` → "flatline" konuşma riski:** PC tarafı `valid` byte'ını **her
  katmanda** onurlandırıyor — `llm.py` `vitals.valid`'e göre dallanıyor, 0/0 asla
  prompt'a girmiyor; GUI "Could not get a reliable reading" gösteriyor. Güvenli.
- **MIN_BEATS+MAX_CV kırılganlığı:** Matematik gösteriyor ki tek kaçan beat CV'yi
  ~0.35'te tutuyor (<0.40 eşiği) → pencere geçerli kalıyor. Çürütüldü.
- **PC(4096) vs FW(1024) payload uyumsuzluğu:** Bugün hiçbir frame >1024 değil
  (AUDIO_DOWN ~528 B); parser temiz resync ediyor, "cascade corruption" iddiası
  yanlış. Sadece kozmetik (paylaşılan sabit eklenebilir).
- **FSM mutex-timeout → IDLE fallback:** Kritik bölümler tek-byte kopya, ns sürer;
  100 ms timeout pratikte imkânsız. Defensive nit.
- **`_audio_chunks` thread-safety:** CPython GIL altında `b"".join` güvenli
  (ampirik test edildi: 0 hata). Sorun değil.
- **`send_audio_down` session-reset heuristiği:** İnsan-tetiklemeli + `_busy`
  serialize edildiği için iki cevap 1 s içinde art arda gelemez → burst olmaz.
- **`system_prompt.txt` güvenlik korumaları:** Yeterli (ilaç dozu yok, teşhis yok,
  "may be" dili, acil servis çağrısı, vitals değerlendirmesi). Değişiklik gerekmez.
- **FIFO drain (RED/IR sırası, 18-bit mask) ve PPG float matematiği:** Doğru,
  overflow yok.

---

## Önerilen sıra (yarına kadar)

1. **T1–T4** (rename, project_link, numpy, README) + `RAPOR_DUZELTMELERI.md` #1
   (Bölüm 7 PPG) — bunlar doğrudan teslim/puan.
2. **D1 donanım kontrolü** (parmakla vitals gerçekten geliyor mu? — demo öncesi
   mutlaka bir kez canlı dene) + **D2/D3** (Ollama görünürlük + port kapatma) —
   demo gününü kurtarır.
3. Zaman kalırsa **C1–C4** temizlik, sonra **O1–O7**.

---

*Hazırlandı: 2026-05-31 — kaynak: adversarial defect-hunt workflow'u (34 ajan,
her yüksek/orta bulgu şüpheci doğrulamadan geçti). Önem dereceleri doğrulama
sonrası düzeltilmiş değerlerdir.*
