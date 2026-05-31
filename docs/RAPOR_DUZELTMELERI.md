# Rapor Düzeltmeleri — `IOT_final.docx`

> **Amaç:** Teslim raporundaki (`IOT_final.docx`) iddiaları, kodun **gerçek güncel
> durumuyla** birebir uyumlu hâle getirmek. Aşağıdaki bulgular, kod tabanını
> dosya-dosya okuyan + raporla karşılaştıran 10 ajanlı bir doğrulamadan çıktı.
>
> **Nasıl kullanılır:** Her madde için *Konum → Mevcut metin → Sorun → Önerilen
> yeni metin* var. Önerilen metinler **İngilizce** (rapor İngilizce-only) ve
> doğrudan yapıştırılabilir. Bu dosya bir çalışma notudur, teslim edilmez.
>
> **Genel değerlendirme:** Rapor büyük oranda **doğru ve dürüst**; pin tablosu,
> 4 kod snippet'i, AES kapsamı, 16 kHz/stereo-32bit, two-plane I/O, USB-OTG→UART
> hikâyesi, FSM ve PC stack kodla **birebir** uyumlu. Aşağıdakiler düzeltilirse
> rapor hem teknik olarak hatasız hem de yapılan işi tam yansıtan bir belge olur.

---

## Öncelik özeti

| # | Düzeltme | Bölüm | Önem | Tür |
|---|----------|-------|------|-----|
| 1 | Vitals "modulo placeholder" iddiası → gerçek PPG algoritması | §7 (+§8) | 🔴 Yüksek | Yanlış bilgi |
| 2 | `project_link.txt` referansı — ✅ ÇÖZÜLDÜ (URL dolduruldu) | §5 + Appendix B | ✅ | Tutarsızlık |
| 3 | Kapak tarihi / dosya adı / teslim tarihi | Kapak | 🟠 Orta | Teslim formatı |
| 4 | Referanslar bölümü çok zayıf (2 link) | §10 | 🟡 Düşük | Kalite/puan |
| 5 | Opsiyonel güçlendirmeler (AES kapsamı, anahtar, vb.) | §3.3/§8 | 🟢 Opsiyonel | Cila |

---

## 🔴 1. Bölüm 7 — Vitals "modulo placeholder" iddiası YANLIŞ

**En kritik düzeltme.** Rapor, gerçekte var olan tam PPG algoritmasını "sahte
placeholder" diye anlatıp **kendi işini küçümsüyor**. Bu hem yanlış bilgi hem de
puan kaybı.

**Konum:** Bölüm 7 (Results and Discussion), vitals paragrafı.

**Mevcut metin (yanlış):**
> *"Sensor data is more nuanced. The MAX30102 raw FIFO readings are real, but our
> current firmware estimates heart rate and SpO₂ with a **simplified placeholder
> calculation (modulo-based approximation), not a calibrated PPG peak-detection
> algorithm**. The displayed values are reasonable-looking demo numbers; they
> respond to the presence of a finger on the sensor but should not be treated as
> medically accurate. We pass them to the LLM with an explicit note in the prompt
> that they are estimates for demonstration only. **Proper PPG analysis (peak
> detection, red/IR ratio for SpO2) is listed as future work.** So the sensor is
> useful for demonstrating that vitals can be part of the first-aid flow, but we
> do not claim clinical accuracy for the submitted firmware."*

**Sorun:** `firmware/main/ppg.c` + `sensor.c` **gerçek bir PPG DSP hattı**
içeriyor: one-pole high-pass DC blocker (α=0.97), 5-tap smoothing, refractory'li
(0.5 s, dicrotic notch reddi) peak detection, IBI filtreleme (300–2000 ms),
coefficient-of-variation güven kapısı (≤0.40), median IBI → BPM (30–200 clamp);
SpO₂ için gerçek ratio-of-ratios (`R = (AC_red/DC_red)/(AC_ir/DC_ir)`,
`SpO2 = 104 − 17·R`, 70–100 clamp); iki aşamalı parmak algılama; ve boot'ta bir
`ppg_self_test()` (72 BPM sentetik sinyal → 67–77 BPM doğrulaması, düz sinyal →
invalid). **Hiçbir yerde modulo/sahte değer yok.** Git geçmişi de teyit ediyor:
"Tune PPG peak detection on real hardware", "16384nA ADC range, finger-gated
buffer". Tek dürüst future-work kalan şey **fabrika kalibrasyonu**.

> ⚠️ Ayrıca **§3.2 ile iç tutarsızlık** var: §3.2 zaten doğru biçimde *"our
> firmware computes heart rate and SpO₂ estimates in software"* diyor. §7'yi
> düzeltmek bu çelişkiyi de giderir.

**Önerilen yeni metin (İngilizce, yapıştırılabilir):**

```
Sensor data is more nuanced but is computed by a real PPG pipeline, not a
placeholder. The MAX30102 RED/IR FIFO readings are drained in software with
finger-gating (a raw-IR floor rejects samples taken without solid skin contact).
Heart rate is then derived by a genuine peak-detection algorithm: a one-pole
high-pass filter removes the DC baseline, a short moving average smooths the
pulsatile signal, and local maxima are detected with a 0.5 s refractory period
that rejects the dicrotic notch. The inter-beat intervals are filtered and the
heart rate is taken from their median (with a coefficient-of-variation gate that
marks an irregular or noisy window as invalid rather than reporting a wrong
number). SpO2 is a ratio-of-ratios estimate, R = (AC_red/DC_red)/(AC_ir/DC_ir)
with SpO2 = 104 - 17R. A boot-time self-test confirms the pipeline recovers a
synthetic 72 BPM signal and rejects a flat (no-finger) signal.

On real hardware the heart rate tracked a reference reading within a few BPM. We
still describe SpO2 as an "uncalibrated estimate" because the linear R-to-SpO2
mapping is a textbook approximation that has not been calibrated against a
medical reference oximeter, and we tell the LLM (in the prompt) to treat the
vitals as estimates for demonstration only. The only genuinely remaining future
work on this path is clinical calibration and better motion-artifact handling,
not the peak detection itself, which is implemented. We do not claim clinical
accuracy for the submitted firmware, but the vitals are produced by real PPG
analysis and are a working part of the first-aid flow.
```

**Bağlı düzeltme — Bölüm 8 (Conclusion), future work:**
Mevcut: *"The MAX30102 path could be calibrated against a reference oximeter and
motion handling could be improved."* → Bu **zaten doğru** (kalibrasyon gerçekten
future work). Sadece §7 düzeltilince tutarlı kalır; istersen "calibrated against
a reference oximeter (the peak-detection and R-ratio computation are already
implemented)" diye netleştir.

---

## 🟠 2. Bölüm 5 + Appendix B — `project_link.txt` referansı tutarsız

**Konum:** Bölüm 5 (Circuit Design) ilk cümle + Appendix B + References.

**Mevcut metin:**
> §5: *"...the live project link is provided in the submitted **wokwi/project_link.txt**."*
> Appendix B: *"The live project link is: https://wokwi.com/projects/464462040095424513 **(also in wokwi/project_link.txt)**."*

**Sorun:** `wokwi/project_link.txt` dosyası **hâlâ placeholder**:
`https://wokwi.com/projects/YOUR_PROJECT_ID`. Yani rapor "linki şu dosyada"
diyor ama dosyada gerçek link yok. Grader dosyayı açarsa boş bulur.

**✅ ÇÖZÜLDÜ.** URL kullanıcı tarafından doğrulandı
(`https://wokwi.com/projects/464462040095424513`) ve `wokwi/project_link.txt`
bu URL ile dolduruldu (placeholder kaldırıldı). Böylece rapordaki *"also in
wokwi/project_link.txt"* ifadesi artık **doğru** → **rapor metninde değişiklik
gerekmez.** Yine de Word'de **Figure 5.1**'in gerçekten bu projenin screenshot'ı
olduğunu görsel olarak teyit etmen yeterli.

---

## 🟠 3. Kapak — dosya adı, öğrenci adı, teslim tarihi

**Konum:** Kapak sayfası + teslim dosya adı.

**Bulgular:**
1. **Dosya adı yanlış.** Hoca: `CEN322_Project_YourName_YourSurname.docx`. Mevcut:
   `IOT_final.docx`. İki yazar olduğu için ör.
   `CEN322_Project_Beyza_Aydin_Ahmet_Bera_Celik.docx` (veya hocanın istediği
   tek-isim formatına göre). *(Yeniden adlandırma `PROJE_YAPILACAKLAR.md` #3'te.)*
2. **`~$IOT_final.docx` benzeri lock dosyası teslim edilmemeli** (Word açıkken
   oluşan geçici dosya).
3. **Teslim tarihi tutarsızlığı.** Kapak **"Due Date 02.06.2026"** diyor; hocanın
   yönergesi **01 JUNE 2026, 23:59**. Bugün **31 Mayıs 2026**. LMS'teki gerçek
   tarihi teyit et; güvenli tarafta kalmak için **1 Haziran**'a kadar yükle.
   Kapak tarihini de yönergeyle eşitlemek mantıklı (02.06 → 01.06 veya gerçek
   teslim tarihin).

---

## 🟡 4. Bölüm 10 — Referanslar çok zayıf

**Konum:** Bölüm 10 (References).

**Mevcut:** Sadece 2 URL (proje GitHub + Wokwi). Part 1 (50 puan) "hardware/
software explanation" istiyor ve sistem promptunuz AHA/Red Cross/WHO kılavuzlarına
atıf yapıyor — ama raporda hiç kaynak yok. Hocanın şablon örneği bile 4 referans
veriyor.

**Öneri:** En az şunları ekle (offline yapıldığı için bunlar arka plan/teknik
kaynaklar):
```
- Espressif. ESP-IDF Programming Guide (I2S, I2C, RMT, mbedTLS). docs.espressif.com
- SYSTRAN. faster-whisper (CTranslate2 Whisper). github.com/SYSTRAN/faster-whisper
- Ollama. Llama 3.2 model library. ollama.com/library/llama3.2
- Rhasspy. Piper text-to-speech. github.com/rhasspy/piper
- Analog Devices/Maxim. MAX30102 Pulse Oximeter Datasheet.
- TDK InvenSense. INMP441 MEMS Microphone Datasheet.
- Analog Devices/Maxim. MAX98357A I2S Class-D Amplifier Datasheet.
- American Heart Association / WHO Basic Emergency Care — first-aid guideline basis for the system prompt.
```
(GitHub + Wokwi linklerini de tut.)

---

## 🟢 5. Opsiyonel güçlendirmeler (cila — puan kaybı yok ama artı)

Bunlar zorunlu değil; raporu daha eksiksiz/savunulabilir yapar:

1. **§3.3 — AES kapsamını netleştir (savunmacı).** Şu an "audio and sensor
   payloads are AES-256-CBC encrypted" — **doğru ve isabetli** (her şeyi
   şifreliyoruz demiyor). İstersen bir cümle ekle:
   > *"Control and status frames are sent in cleartext; only audio and sensor
   > payloads are encrypted. A fresh random IV is prepended to each encrypted
   > payload. The AES key/IV are fixed demo constants for the course, not a
   > production key-exchange scheme."*
   Bu, olası bir "security review" sorusunu önceden cevaplar.

2. **§7/§3.3 — "uncalibrated estimate" ifadesini tutarlı kullan.** §7 düzeltmesi
   bunu zaten getiriyor; LLM'e vitals'ın "uncalibrated estimate" olarak verildiğini
   (kod gerçeği) söylemek dürüst ve tutarlı.

3. **Figure 5.2 (Physical prototype) doğrula.** Metinde "Figure 5.2 — Physical
   prototype" var ama gömülü görseller image1.jpeg/image2.png/image3.png. Word'de
   gerçek bir fiziksel kurulum fotoğrafı yerleşmiş mi kontrol et; yoksa ya foto
   ekle ya caption'ı kaldır.

4. **§6 başlığı "Arduino Codes and Explanation".** Şablon başlığı bu; siz ESP-IDF
   kullanıyorsunuz ve bunu §6'da açıkça gerekçelendirmişsiniz (iyi). Başlığı
   şablonla aynı bırakmak güvenli; istersen "(ESP-IDF, instructor-approved)"
   parantezi ekleyebilirsin.

---

## Kod snippet'leri — DOĞRU (değişiklik gerekmez)

Bilgi amaçlı: §6.2'deki 4 snippet (start_recording, task_audio_in,
protocol_frame_pack, PC `work()` pipeline) gerçek kodla **birebir** eşleşiyor.
Pin tablosu (§5) `config.h` ile **tam** uyumlu. Bunlara dokunma.

---

*Hazırlandı: 2026-05-31 — kaynak: 10 ajanlı kod-rapor doğrulama workflow'u.*
*Diğer (kod/dokümantasyon/teslimat) işler için bkz. `PROJE_YAPILACAKLAR.md`.*
