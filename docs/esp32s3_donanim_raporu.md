# ESP32-S3 Projesi Donanım Teknik Raporu

> Bu rapor, ESP32-S3-DevKitM-1-N8 mikrodenetleyici üzerine kurulu bir proje için satın alınan donanım modüllerinin teknik özelliklerini, pin yapısını, çalışma prensiplerini ve dikkat edilmesi gereken noktaları özetler. Rapor; resmi datasheet'ler (Analog Devices/Maxim, TDK InvenSense, Worldsemi, Espressif), Adafruit/SparkFun referansları ve genel topluluk uygulama notları temel alınarak hazırlanmıştır.

İçindekiler:

1. ESP32-S3-DevKitM-1-N8 (Ana kart)
2. MAX30102 — Nabız ve SpO2 sensörü
3. INMP441 — I2S MEMS mikrofon
4. MAX98357A — I2S Class-D ses amplifikatörü
5. WS2812B 8-bit NeoPixel Halka
6. Lojik Seviye Dönüştürücü (3.3 V ↔ 5 V, BSS138)
7. 4 Ω / 3 W / 40 mm Hoparlör
8. Sistem Bütünlüğü ve Genel Uyarılar
9. Kaynaklar

---

## 1. ESP32-S3-DevKitM-1-N8 (Ana Kart)

### Modül Özeti

DevKitM-1, üzerinde **ESP32-S3-MINI-1** modülü taşıyan giriş seviyesi bir geliştirme kartıdır. Modülün üzerindeki SiP yonga **ESP32-S3FN8**'dir. `N8` soneki paketin **8 MB dahili Quad SPI flash** içerdiğini ve **PSRAM içermediğini** belirtir. Yani büyük ses tamponları veya görüntü işleme gibi PSRAM gerektiren işler bu varyantta sınırlıdır.

| Özellik | Değer |
|---|---|
| MCU | Çift çekirdek Xtensa LX7 @ 240 MHz |
| SRAM | 512 KB |
| Flash | 8 MB (dahili SiP) |
| PSRAM | Yok |
| Kablosuz | Wi-Fi 2.4 GHz b/g/n + BLE 5 (LE) |
| GPIO seviyesi | 3.3 V (5 V toleranslı **DEĞİL**) |
| USB | 1× USB 2.0 OTG Full-Speed, 1× USB Serial/JTAG (her ikisi GPIO19/20 üzerinde) |
| Periferik | 2× I2C, 2× I2S, 2× SPI, 4× RMT TX + 4× RMT RX, 3× UART, 2× SAR ADC (20 kanal), 8× LEDC, 14× Touch |
| USB konektörü | Çoğunlukla 2 adet: biri CP210x/CH340 (UART), diğeri yerel USB |

İki bağımsız I2S birimi olması, bu proje için kritik bir avantajdır: mikrofon (INMP441) bir I2S birimine, amplifikatör (MAX98357A) diğerine atanarak **eşzamanlı kayıt + çalma** rahatlıkla yapılabilir.

### Kritik / Kaçınılması Gereken GPIO'lar

| GPIO | Durum | Açıklama |
|---|---|---|
| **GPIO0** | Strapping (boot mode) | Açılışta 0 = bootloader, 1 = uygulama. Bağımlı bir devre LOW çekmemeli. |
| **GPIO3** | Strapping (JTAG kaynağı) | Mecbur kalmadıkça başka amaçla kullanmayın. |
| **GPIO45** | Strapping (VDD_SPI) | Genelde boş bırakın. |
| **GPIO46** | Strapping (boot mode log) | Açılışta HIGH çekilmemeli; sakınılması iyi. |
| **GPIO19 / GPIO20** | USB D− / D+ | USB-OTG veya USB-JTAG kullanılıyorsa rezerve. CP210x portundan programlama yapıyorsanız serbest. |
| **GPIO26 – GPIO32** | Dahili 8 MB flash hatları | **Asla kullanılmaz**, kart bozulur. |
| **GPIO33 – GPIO37** | MINI-1 paketinde **bağlı değildir** | Header'da görünse bile elektriksel olarak NC. |
| **GPIO43 / GPIO44** | UART0 TX/RX (boot logları) | Başka amaçla kullanılabilir ama log kaybedilir. |
| **GPIO48** | Kart üzerindeki RGB LED (WS2812) | Boş bırakılabilir veya doğrudan ek NeoPixel için kullanılabilir. |

ESP32-S3'te eski ESP32'nin aksine **giriş-only pin yoktur**: kullanılabilir tüm GPIO'lar hem giriş hem çıkış yapabilir. RTC-capable olanlar GPIO0–21 aralığındadır (deep-sleep wake-up, ULP).

### Elektriksel Sınırlar

- GPIO başına maksimum **çıkış akımı ≈ 40 mA** (sürücü ayarı 3'te), tipik güvenli kullanım 12–20 mA.
- Kart üzerindeki **3.3 V LDO** revizyona göre ~500–800 mA verebilir; ağır periferikler (hoparlör amplifikatörü, NeoPixel) bu hattan değil **5V/VBUS** üzerinden beslenmelidir.
- **5V pini doğrudan USB VBUS**'a bağlıdır; PC USB portu 500–900 mA arasıyla sınırlıdır. Hem amplifikatör hem ring tam yüklü çalışacaksa harici 5 V (en az 2 A) önerilir. Aynı anda hem USB hem harici 5 V vermek için ya USB'yi çıkarın ya da bir schottky diyot ile yön ayrımı yapın — aksi takdirde PC USB portuna ters akım yürür.

---

## 2. MAX30102 — Nabız ve Oksimetre (SpO2) Sensörü

### Çalışma Prensibi

MAX30102, parmak/bilek/kulak memesi gibi bir dokuya **kırmızı (660 nm)** ve **kızılötesi (880 nm)** LED ile ışık gönderip dokudan yansıyan ışığı dahili fotodiyot ile ölçen optik bir PPG (fotopletismografi) sensörüdür.

- Yansıyan sinyaldeki nabız modülasyonu **BPM** (kalp atış hızı) verir.
- Kırmızı ve IR sinyallerin AC/DC oranlarından **SpO2** hesaplanır. Tipik basit yaklaşım: `R = (AC_red/DC_red) / (AC_ir/DC_ir)` ve ardından kalibrasyonlu bir formül (örn. `SpO2 ≈ 110 − 25·R`).

### Temel Özellikler

| Özellik | Değer |
|---|---|
| ADC çözünürlüğü | 18-bit |
| Örnekleme hızı | 50 – 3200 sps (programlanabilir) |
| LED akımı | 0 – 50 mA (register ile ayarlanır) |
| FIFO derinliği | 32 örnek |
| Arayüz | I²C, 400 kHz Fast Mode |
| I²C adresi | **0x57** (sabit) |
| Çekirdek VDD | 1.7 – 2.0 V |
| LED VLED | 3.1 – 5.25 V |
| Aktif akım | ~600 µA + LED akımı |
| Shutdown akımı | ~0.7 µA |

GY-MAX30102 modüllerinde (en yaygın breakout) **dahili LDO + I²C pull-up'lar** vardır. VIN pinine 3.3 V veya 5 V uygulanabilir; ancak SDA/SCL pull-up'ları modülde **3.3 V hattına** çekilidir, dolayısıyla ESP32-S3 ile **3.3 V besleme** en güvenli seçimdir.

### Pin Yapısı (GY-MAX30102 modülü)

| Pin | İşlev |
|---|---|
| **VIN** | 1.8 – 5.5 V besleme (modülde regülatör vardır) |
| **GND** | Toprak |
| **SCL** | I²C clock |
| **SDA** | I²C data |
| **INT** | Açık-drain aktif-LOW kesme çıkışı (modülde pull-up var) |
| **IRD / RD** | İç LED sürücü test pinleri — normalde boş bırakılır |

### Önemli Notlar ve Sık Yapılan Hatalar

- **5 V uyarısı:** Çip 5 V toleranslı **değildir**. Breakout'ta regülatör olsa bile pull-up'ların hangi hatta çekildiğine dikkat edin; 5 V hatta pull-up'lı bir varyant ESP32-S3 GPIO'larını öldürebilir.
- **I²C pull-up çakışması:** Birden fazla modül aynı I²C hattına bağlanırsa pull-up dirençleri paralel olur ve eşdeğer direnç çok düşebilir (örn. 2 kΩ altı). Toplam 1.5–4.7 kΩ tipiktir; gerekirse fazla pull-up'lar lehimden kaldırılır.
- **Tek modül kısıtı:** I²C adresi sabittir (0x57). Aynı hatta iki MAX30102 takılamaz; mux gerekir.
- **FIFO yönetimi:** `FIFO_WR_PTR`, `FIFO_RD_PTR` ve `OVF_COUNTER` doğru senkron tutulmalı; aksi halde eski veri okunur veya overflow olur. INT pinini kesme olarak kullanmak en verimlisidir; status register okunduğunda bayrak temizlenir.
- **LED akımı:** Varsayılan değerler genelde zayıf gelir; tipik iyi sonuç için kırmızı/IR ~24–30 mA civarı önerilir. Çok yüksek akım fotodiyotu doyurur (saturasyon).
- **Fiziksel kullanım:** Parmak düz ve sabit, ne çok sıkı ne çok gevşek; sensör penceresi izopropil alkol + yumuşak bezle temizlenmeli. Soğuk parmakta düşük perfüzyon nedeniyle sinyal zayıflar.
- **Filtreleme:** Ham PPG sinyali DC offset ve hareket artefaktı içerir. BPM/SpO2 hesabı öncesi 0.5–5 Hz bandpass + DC giderme yapılmalıdır. Maxim'in "MAXREFDES117" referans algoritması veya SparkFun MAX3010x kütüphanesi iyi başlangıçtır.

### ESP32-S3 Bağlantı Önerisi

| MAX30102 | ESP32-S3 (öneri) |
|---|---|
| VIN | 3V3 |
| GND | GND |
| SDA | GPIO 8 |
| SCL | GPIO 9 |
| INT | GPIO 4 (boş bir GPIO) |

---

## 3. INMP441 — I2S MEMS Omnidirectional Mikrofon

### Çalışma Prensibi

INMP441, içinde MEMS ses sensörü, anti-aliasing filtre, 24-bit ΔΣ ADC ve standart I²S arayüzü barındıran **tamamen dijital** bir mikrofondur. Analog ses hattı olmadığı için kablo gürültüsünden büyük ölçüde bağışıktır. Alt-port tipidir; PCB'deki delik kapatılmamalıdır.

### Temel Özellikler

| Özellik | Değer |
|---|---|
| Besleme | 1.8 – 3.3 V |
| Akım | ~1.4 mA (normal), ≤ 1 µA (sleep) |
| SNR | 61 dBA |
| Hassasiyet | −26 dBFS @ 94 dB SPL |
| Frekans yanıtı | 60 Hz – 15 kHz (−3 dB) |
| AOP | 120 dB SPL |
| Çıkış | 24-bit Philips I2S, MSB-first, two's complement |
| BCLK | 512 kHz – 3.42 MHz (tipik 1.024–2.048 MHz) |
| MCLK | **Gerekmez** |

### Pin Yapısı

| Pin | İsim | Görev |
|---|---|---|
| 1 | **VDD** | 1.8 – 3.3 V besleme |
| 2 | **GND** | Toprak |
| 3 | **L/R** | Sol/sağ kanal seçimi: GND = sol, VDD = sağ |
| 4 | **WS** | Word Select (LRCLK), MCU üretir |
| 5 | **SCK** | Bit Clock (BCLK), MCU üretir |
| 6 | **SD** | Veri çıkışı (DOUT), mikrofondan MCU'ya |
| 7 | **NC** | Bağlanmaz |

INMP441 **her zaman I2S slave** olarak çalışır.

### Önemli Notlar

- **L/R asla boşta bırakılmaz.** Mutlaka GND'ye veya VDD'ye çekilmelidir. Yazılımdaki I2S "slot" konfigürasyonu da bununla tutarlı olmalıdır; aksi halde sürekli sıfır veya kararsız veri okunur.
- **5 V toleranssızlığı:** VDD veya I2S sinyallerine 3.3 V üstü uygulamak çipi öldürür.
- **BCLK alt sınırı:** BCLK 512 kHz'in altına düşerse mikrofon sleep moduna geçer. Tipik 16 kHz örnekleme için 32-bit slot × 2 kanal = ~1.024 MHz BCLK iyi bir taban değerdir.
- **Stereo kullanımı:** İki INMP441 paralel; biri L/R→GND (sol), diğeri L/R→VDD (sağ). SD hatları paralel bağlanır.
- **Veri zamanlaması:** Veri BCLK yükselen kenarda geçerlidir; MCU düşen kenarda örnekler. Yazılım/sürücü tarafında standart Philips I2S seçilmelidir.
- **EMI/yerleşim:** I²S kabloları uzun ise BCLK/WS hatlarını GND ile çevreleyin. Class-D amplifikatörün anahtarlama gürültüsü mikrofona girebilir; modülü amplifikatör/hoparlör kablolarından uzak tutun.

### ESP32-S3 Bağlantı Önerisi (I2S RX)

| INMP441 | ESP32-S3 (öneri) |
|---|---|
| VDD | 3V3 |
| GND | GND |
| L/R | GND |
| WS | GPIO 42 |
| SCK | GPIO 41 |
| SD | GPIO 2 |

---

## 4. MAX98357A — I2S Class-D Mono Ses Amplifikatörü

### Çalışma Prensibi

MAX98357A, I2S girişli **DAC + Class-D amplifikatör birleşimi** mono bir entegredir. Filtresiz "spread-spectrum edge-rate control" modülasyonu sayesinde çıkışına LC filtre olmadan doğrudan 4–8 Ω hoparlör bağlanır. **MCLK gerektirmez**; tüm dahili saatleri BCLK'den PLL ile üretir. Çıkış **BTL (Bridge-Tied Load)** olduğundan iki ucu da hoparlöre gider; **OUT− asla GND'ye bağlanmaz**, aksi halde entegre kalıcı hasar görür.

### Temel Özellikler

| Özellik | Değer |
|---|---|
| Besleme | 2.5 – 5.5 V (tam güç için 5 V) |
| Quiescent akım | ~2.4 mA |
| Shutdown akımı | < 5 µA |
| Çıkış gücü | 3.2 W @ 4 Ω, %10 THD (2.5 W @ %1 THD) |
| Çıkış gücü | 1.8 W @ 8 Ω, %10 THD |
| Verim | ~%92 |
| THD+N | %0.013 @ 1 W, 8 Ω |
| Hoparlör empedansı | 4 – 8 Ω |
| Veri formatı | I2S / Left-justified / TDM, 16/24/32-bit, 8 – 96 kHz |
| Koruma | Aşırı sıcaklık, aşırı akım, click & pop |

### Pin Yapısı (Breakout)

| Pin | Görev |
|---|---|
| **VIN** | 2.5 – 5.5 V besleme |
| **GND** | Toprak |
| **SD / Mode** | Shutdown + kanal seçim (analog seviyeli) |
| **GAIN** | Kazanç ayarı (3–15 dB) |
| **DIN** | I2S veri girişi |
| **BCLK** | I2S bit clock |
| **LRC** | I2S word select |
| **+ / −** | Hoparlör çıkışı (BTL) |

### GAIN Pin Konfigürasyonu

| Bağlantı | Kazanç |
|---|---|
| 100 kΩ direnç ile GND | 15 dB |
| Doğrudan GND | 12 dB |
| Boşta (varsayılan) | 9 dB |
| Doğrudan VIN | 6 dB |
| 100 kΩ direnç ile VIN | 3 dB |

### SD Pininin Üç Rolü

SD pini hem **shutdown anahtarı** hem **kanal seçici** gibi davranır; gerilimine göre mod değişir:

| SD gerilimi | Mod |
|---|---|
| < 0.16 V | Tamamen kapalı (shutdown) |
| 0.16 – 0.77 V | Stereo karışım (L+R)/2 — mono çıkış (Adafruit modülünde varsayılan) |
| 0.77 – 1.4 V | Sadece sağ kanal |
| > 1.4 V | Sadece sol kanal |

Çoğu breakout, dahili 100 kΩ pull-down + 1 MΩ pull-up ile **mono mix** modunda gelir. SD pinini bir GPIO'ya çekip LOW çekerek **soft-mute** ve pop önleme yapmak iyi bir alışkanlıktır.

### Önemli Notlar

- **OUT− GND'ye bağlanmaz!** BTL çıkışı, her iki ucu hoparlöre gider.
- **MCLK üretmeyin.** ESP32-S3 I2S sürücüsünde sadece BCLK + LRC + DIN yeterlidir.
- **Besleme dekuplajı:** 5 V girişinde en az **100 µF elektrolit + 100 nF seramik** kondansatör. Tepe akımı 4 Ω hoparlörle 1 A üstüne çıkabilir; USB beslemesi sınırda olabilir.
- **Otomatik shutdown:** I2S verisi (LRC/BCLK) ~5 sn boyunca durursa çip otomatik kapanır ve "click" sesi duyulabilir. Sessiz aralıklarda SD pinini LOW çekmek temiz çözümdür.
- **3.3 V ile çalışma:** Mümkün ama çıkış gücü ciddi şekilde düşer (~%50). En iyi performans 5 V'tadır.
- **I2S sinyalleri 5 V tolerant değildir;** ESP32-S3'ün 3.3 V çıkışı zaten doğru.
- **DIN boşta bırakılmamalı:** Kullanılmıyorsa GND'ye çekin, yoksa gürültü/parazit duyulur.

### ESP32-S3 Bağlantı Önerisi (I2S TX)

| MAX98357A | ESP32-S3 (öneri) |
|---|---|
| VIN | 5V (VBUS) |
| GND | GND (mikrofon ile ortak) |
| DIN | GPIO 7 |
| BCLK | GPIO 5 |
| LRC | GPIO 6 |
| GAIN | Boşta (9 dB) |
| SD | GPIO 8 (opsiyonel, mute) |

---

## 5. WS2812B 8-Bit NeoPixel Halka (RGB LED)

### Çalışma Prensibi

WS2812B, içine entegre kontrol IC'si ve RGB LED'leri birleştirmiş **adreslenebilir** bir LED'dir. Her LED ilk gelen 24 bit'i (kendi rengi, GRB sırasında, MSB-first) kapar, kalan veriyi DOUT üzerinden bir sonrakine geçirir. Tek bir data hattıyla onlarca/yüzlerce LED zincirlenebilir.

Bit kodlaması NRZ tabanlıdır:
- "0" biti: ~0.4 µs HIGH + ~0.85 µs LOW
- "1" biti: ~0.8 µs HIGH + ~0.45 µs LOW
- RESET / latch: > 50 µs LOW (V5 sürümünde > 280 µs)

Tolerans ±150 ns mertebesindedir; bu yüzden donanım destekli iletim (ESP32-S3'te **RMT** ideal, alternatif SPI/I2S DMA) kullanılır.

### Temel Özellikler

| Özellik | Değer |
|---|---|
| Besleme | 3.5 – 5.3 V (nominal 5 V) |
| Renk derinliği | 24-bit (8-bit × R/G/B) |
| Veri hızı | 800 kbps |
| LED başına idle akım | ~1 mA |
| LED başına tam beyaz akım | ~60 mA (3 × 20 mA) |
| 8-LED tam beyaz tepe akımı | ~480 mA |
| Data HIGH eşiği (V_IH) | min. 0.7 × VDD (5 V için ~3.5 V) |

### Bağlantı Noktaları

| Pin | İşlev |
|---|---|
| **VCC / 5V / +** | 5 V besleme |
| **GND / −** | Toprak |
| **DIN** | Data girişi (mikrodenetleyiciden) |
| **DOUT** | Data çıkışı (zincirleme için bir sonraki halkaya) |

### 3.3 V Data İle Sürme Konusu (Bu projenin kilit noktası)

Datasheet V_IH için **0.7 × VDD** ister. 5 V besleme ile bu **3.5 V** demektir; ESP32-S3'ün 3.3 V GPIO çıkışı bu eşiğin **altındadır**. Pratikte üç çözüm vardır:

1. **Doğrudan sürme (riskli):** Birçok partide çalışır, ama parti/sıcaklığa göre kararsızdır. İlk LED'in yanıp sönmesi/yanlış renk yaygın belirtidir.
2. **VDD düşürme:** WS2812B'yi 4.5 V veya bir Schottky diyot üzerinden ~4.3 V ile beslemek V_IH eşiğini 3.0–3.15 V'a indirir. Hızlı bir hack çözümdür.
3. **Level shifter (önerilen):** **74AHCT125, 74HCT245** veya **SN74LVC1T45** gibi tek-yönlü, 5 V besleme + 3.3 V girişi geçerli HIGH olarak algılayan tampon IC'ler en sağlam yoldur.

> **Önemli:** Projenizde olan **BSS138 tabanlı bi-directional level converter, WS2812B için önerilmez.** RC zaman sabiti (10 kΩ pull-up × kablo kapasitansı) 800 kbps NRZ'nin gerektirdiği keskin yükselen kenarları tutturamaz; veri bozulur. Bu level converter'ı **I²C/SPI/UART** gibi yavaş veya açık-drain hatlar için saklayın.

### Diğer Önemli Notlar

- **Seri direnç:** DIN'in hemen önüne **220–470 Ω** seri direnç koymak hem ESP32 pinini hem ilk LED'i yansımalardan korur.
- **Dekuplaj:** Halka başına en az 10–100 µF elektrolit + 100 nF seramik; uzun zincirlerde her birkaç LED'de bir 100 nF.
- **Ortak GND:** ESP32-S3 ile WS2812B GND'leri mutlaka birleşmelidir.
- **Sıralama:** Renk sırası GRB'dir; bazı varyantlar (SK6812, WS2812B-V5) farklı zamanlama veya RGBW olabilir.
- **Güç tüketimi:** 8 × 60 mA = 480 mA pratikte USB sınırına yakındır. Parlaklığı yazılımdan %20–30 ile sınırlayarak hem akımı düşürün hem göz konforu kazanın. Beyaz arka plan yerine renkli efektler kullanın.
- **Kart üstü LED çakışması:** DevKitM-1'in GPIO48 üzerinde zaten bir WS2812 vardır. Halkayı **doğrudan GPIO48'e** zincirlemek hem pini boş tutar hem rasyoneldir; ya da farklı bir GPIO (örn. GPIO38) seçilebilir.

### ESP32-S3 Bağlantı Önerisi

| WS2812B Ring | Bağlantı |
|---|---|
| VCC | 5V (VBUS) |
| GND | GND |
| DIN | GPIO 38 (veya GPIO 48) — **level shifter üzerinden 5 V'a** |
| DOUT | Boş (veya başka bir halkanın DIN'i) |

---

## 6. Lojik Seviye Dönüştürücü (3.3 V ↔ 5 V, 4 Kanal, BSS138)

### Çalışma Prensibi

Klasik SparkFun BOB-12009 referansına dayanan, **4 adet BSS138 N-kanal MOSFET + 8 adet 10 kΩ pull-up** kullanan **çift yönlü** bir modüldür. NXP/Philips AN97055 uygulama notunun standardıdır. Her kanal şöyle çalışır:

- LV tarafı LOW çektiğinde: V_GS = 3.3 V eşiği aşar, MOSFET iletime geçer ve HV tarafını da LOW'a çeker.
- HV tarafı LOW çektiğinde: MOSFET'in dahili gövde diyotu üzerinden LV tarafı da LOW'a düşer; bunun ardından MOSFET tam iletime geçer.
- İki taraf da HIGH'da bırakıldığında: Pull-up'lar kanalı kendi referansına (LV = 3.3 V, HV = 5 V) çeker; MOSFET kapalıdır.

Bu, herhangi bir yön sinyali (DIR pini) olmadan otomatik **bi-directional** çalışma sağlar.

### Temel Özellikler

| Özellik | Değer |
|---|---|
| LV tarafı | 1.8 – 3.3 V |
| HV tarafı | 2.8 – 5 V (HV > LV olmalı) |
| Kanal | 4 (bi-directional) |
| Pull-up | 10 kΩ (her tarafta) |
| Tipik güvenli hız | I²C 100/400 kHz, SPI ≤ ~2 MHz |
| MOSFET | BSS138 |

### Pin Yapısı

| LV tarafı | HV tarafı |
|---|---|
| LV (referans, 3.3 V) | HV (referans, 5 V) |
| GND | GND (dahili bağlı) |
| LV1 ↔ HV1 | (kanal 1) |
| LV2 ↔ HV2 | (kanal 2) |
| LV3 ↔ HV3 | (kanal 3) |
| LV4 ↔ HV4 | (kanal 4) |

### Önemli Notlar

- **HV > LV zorunludur.** Aksi halde MOSFET'ler sürekli iletime geçer, sinyaller bozulur.
- **GND ortak:** İki tarafın da GND'si ortak olmalı; modüldeki iki GND zaten dahili bağlıdır.
- **Hız sınırı:** 10 kΩ pull-up + hat kapasitansı yükselen kenarı yavaşlatır. I²C 100/400 kHz sorunsuz; 1 MHz Fast Mode+ için pull-up'ları 2.2 kΩ'a düşürmek gerekir. **>4 MHz SPI ve WS2812B (800 kbps) için bu modül uygun değildir.**
- **Analog için uygun değil:** Sadece dijital sinyal. ADC/DAC çıkışları doğrusal geçirilemez.
- **Pull-up çakışması:** Hatta zaten pull-up'lı bir cihaz (örn. MAX30102 breakout) varsa toplam direnç paralel olarak azalır; çok düşerse ölçüp gerekirse pull-up'lardan birini kaldırın.
- **HV referansı boş bırakılmaz:** Aksi halde HV tarafında geçerli HIGH oluşmaz.
- **Bu projede en doğru kullanım:** Eğer ileride 5 V'lık bir cihazla (örn. eski bir sensör, klasik HD44780 LCD, bir 5 V UART cihazı) iletişim kurarsanız bu modülü orada kullanın. WS2812B için ise yukarıda anlatıldığı gibi **74AHCT125 / 74HCT245** tercih edilmelidir.

---

## 7. 4 Ω / 3 W / 40 mm Hoparlör

### Genel Bakış

Neodyum/ferrit mıknatıslı, kağıt veya mylar diyaframlı, dinamik (moving-coil) mini hoparlördür. MAX98357A'nın hedeflediği 4–8 Ω, 3 W aralığına tam uyar.

### Tipik Özellikler

| Parametre | Değer |
|---|---|
| Empedans | 4 Ω (±%15) |
| Nominal güç | 3 W RMS (kısa tepe ~5 W) |
| Çap | 40 mm |
| Hassasiyet | ~85–88 dB SPL @ 1 W / 1 m |
| Kullanılabilir frekans bandı | ~300 Hz – 8 kHz |
| Rezonans (Fs) | ~400–600 Hz |
| Polarite | Yok (mono kullanımda önemsiz) |

### Önemli Noktalar

- **MAX98357 BTL çıkışına bağlanış:** Hoparlörün iki ucu, amplifikatörün **OUT+ ve OUT−** terminallerine gider. Hiçbir ucu GND'ye bağlanmaz.
- **Polarite:** Tek hoparlörde önemsizdir; iki hoparlörlü kuruluşlarda tutarlı olmalı (faz iptali olmasın).
- **Güç sınırı:** 3 W RMS sürekli güçtür. Klipse giren bir Class-D sinyali bobini hızlıca ısıtır ve diyaframı yakabilir. Yazılım tarafında çıkış seviyesini %80–90 ile sınırlamak ömrü uzatır.
- **Frekans yanıtı sınırı:** Bu boyutta 200 Hz altı pratik olarak yoktur; **200–300 Hz altı için yazılımsal high-pass** uygulamak hem distorsiyonu azaltır hem enerjiyi boşa harcamaz.
- **Montaj:** Kapalı veya en azından hava kaçırmayan bir muhafaza bas yanıtını ciddi şekilde iyileştirir. Açık montajda ön/arka dalgalar birbirini iptal eder.
- **Titreşim izolasyonu:** Çalışırken yarattığı mekanik titreşim, MAX30102 gibi optik ölçüm yapan modüllere bağlanmamalı; ayrı muhafaza/yumuşak conta kullanın.
- **EMI:** Class-D anahtarlama gürültüsü (~300 kHz) hoparlör kablolarından yayılır. Kabloları kısa tutun, mümkünse twisted pair, ve mikrofon/sensör hatlarından uzak çekin.

---

## 8. Sistem Bütünlüğü ve Genel Uyarılar

Tüm modülleri tek bir kartta birleştirirken aşağıdaki noktalara dikkat edin:

- **Lojik seviyeler:** MAX30102, INMP441, MAX98357A ve BSS138 modülünün LV tarafı **3.3 V uyumludur.** WS2812B 5 V tarafındadır — onun data hattı için **özel bir level shifter** (74AHCT125/74HCT245) önerilir; eldeki BSS138 modülü 800 kbps'i kaldıramaz.
- **5 V hat akım bütçesi:** Tam yükte hoparlör amplifikatörü ~600 mA tepe, 8-LED ring ~480 mA tepe çekebilir. Tek USB beslemesi (500 mA tipik PC portu) bunu kaldırmaz. Pratikte:
  - Ring parlaklığını yazılımdan **%25–%50** ile sınırlayın.
  - Hoparlör seviyesini yazılımdan **%80** civarında tutun.
  - Mümkünse **harici 5 V / 2 A** kaynak kullanın; bu durumda USB kablosunu çıkarın ya da bir Schottky diyot ile yön ayrımı yapın.
- **Yıldız topraklama:** Class-D çıkışı yüksek anlık akım çeker. Tüm GND'leri **yıldız (star) noktasında** birleştirin. Aksi halde dönüş akımı INMP441'in analog tarafından geçerek "whine" gürültüsü üretir.
- **I²S iki bus stratejisi:** INMP441'i I2S0 RX, MAX98357A'yı I2S1 TX olarak atayın. Tek bus full-duplex de mümkündür ama her iki çip aynı örnekleme hızında çalışmak zorunda kalır.
- **I²C tek bus:** MAX30102 + ileride eklenecek başka I²C cihazları aynı SDA/SCL'yi paylaşabilir; sadece **adres çakışması** ve **pull-up toplamı** kontrol edilmelidir.
- **Strapping ve flash pinleri:** GPIO0, 3, 19, 20, 26–32, 33–37, 45, 46 — bu pinlerden kaçının veya bilinçli kullanın. Önerilen pin seti (8, 9 / 41, 42, 2 / 5, 6, 7 / 38) bu kısıtlara uyumludur.
- **Mekanik yerleşim:** Hoparlör mümkün olduğunca uzakta; mikrofon hoparlörden ayrı bir köşede ve kabloları kısa olsun. NeoPixel halka opsiyonel olarak ortada veya görsel olarak en uygun yerde, fakat sensörden bağımsız bir güç hattıyla beslenirse en iyisidir (akım dalgalanmasının başka modüllere yayılmaması için).
- **Reset ve boot davranışı:** Modüllerin SD/INT/DIN gibi GPIO'lara bağlı pinleri, reset anında ESP32-S3'ün strapping okumasını bozmamalı. Bu yüzden bu pinleri GPIO0/3/45/46'ya bağlamayın.

---

## 9. Kaynaklar

ESP32-S3:
- ESP32-S3-DevKitM-1 User Guide — docs.espressif.com (esp-dev-kits/esp32s3/esp32-s3-devkitm-1)
- ESP32-S3-MINI-1 / MINI-1U Datasheet v1.7 — documentation.espressif.com
- ESP32-S3 Series Datasheet v2.2 — espressif.com
- ESP32-S3 Technical Reference Manual v1.8 — documentation.espressif.com
- ESP-IDF API Reference (GPIO, I2S) — docs.espressif.com/projects/esp-idf

MAX30102:
- Analog Devices MAX30102 Datasheet — analog.com
- MAXREFDES117 Reference Design — analog.com
- SparkFun MAX3010x Sensor Library — github.com/sparkfun

INMP441:
- TDK InvenSense INMP441 Datasheet — invensense.tdk.com
- InvenSense AN-0208 High-Performance Digital MEMS Microphone Application Note
- Components101 INMP441 Module Guide — components101.com

MAX98357A:
- Analog Devices MAX98357A/B Datasheet — analog.com
- Adafruit MAX98357 I2S Class-D Mono Amp Guide — learn.adafruit.com
- DFRobot Wiki SKU_DFR0954 — wiki.dfrobot.com

WS2812B:
- Worldsemi WS2812B Datasheet (V1.1 ve V5) — worldsemi.com
- Adafruit NeoPixel Überguide — learn.adafruit.com
- 74AHCT125 / 74HCT245 / SN74LVC1T45 Datasheet — TI / NXP

BSS138 Logic Level Converter:
- ON Semi / Diodes BSS138 Datasheet
- SparkFun BOB-12009 Hookup Guide — sparkfun.com
- NXP AN97055 — Bi-directional level shifter for I²C-bus

Hoparlör:
- Motorobit 4 Ω 3 W 40 mm Hoparlör — hepsiburada.com (HBC00000TIGG2)

Hepsiburada ürün sayfaları (Türkiye pazarındaki referanslar):
- Alkatronik MAX98357 I2S 5V 3W Modül — HBC00003G3P9X
- Alkatronik INMP441 I2S MEMS Mikrofon — HBC000037W1K0
- Roboyol 8-bit NeoPixel Halka WS2812B — HBC00006WAURV
- Robo Dünya 3.3/5 V Logic Level Converter — HBC00003Z3XND
