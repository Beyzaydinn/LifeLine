# Yerel Yapay Zekâ Destekli Sesli İlk Yardım Asistanı

## Proje Planı ve Mimari Raporu (v2)

> Bu doküman, CEN322 Internet of Things final projesi için yapılan tasarım toplantılarının sonucunda kararlaştırılan mimarinin kapsamlı özetidir. v2 sürümünde donanım pin atamaları, level shifter konusu, güç bütçesi, geliştirme platformu (ESP-IDF) ve etik yaklaşım güncellenmiştir.

---

## 1. Yönetici Özeti

Bu proje, internet bağlantısı olmadan çalışan sesli bir ilk yardım rehberi geliştirmeyi amaçlamaktadır. Sistem iki bileşenden oluşur: kullanıcının fiziksel olarak etkileşime girdiği bir ESP32-S3 tabanlı saha cihazı ve ağır yapay zekâ işlemlerinin yapıldığı, USB üzerinden cihaza bağlı bir yerel bilgisayar. Tüm hesaplama yerelde yürütülür; bulut servisi, API anahtarı veya kablosuz ağ kullanılmaz.

Sistem **push-to-talk butonu** ile aktive olur (proje ileri aşamasında ek bir wake word katmanı eklenecektir). Aktive olduktan sonra mikrofon kullanıcının sözel açıklamasını kaydeder, eş zamanlı olarak MAX30102 sensörü hastanın nabız ve oksijen seviyesini ölçer. Bu veriler USB üzerinden bilgisayara aktarılır; bilgisayarda lokal olarak çalışan Whisper konuşma tanıma modeli, Llama 3.2 dil modeli ve Piper ses sentezi sırayla işleyerek ilk yardım talimatlarını sesli olarak cihaza geri gönderir. Cihaz, talimatları MAX98357 amfi ve hoparlör üzerinden çalarken NeoPixel halka sistemin durumunu renkli ışıklarla bildirir.

Final teslim tarihi **1 Haziran 2026**'dır.

---

## 2. Tasarım Felsefesi ve Genel Yaklaşım

Mimarinin temelinde **"sahada hafif, beyinde ağır"** prensibi yatar. ESP32-S3-DevKitM-1-N8 düşük kaynaklı bir mikrodenetleyicidir; PSRAM içermediği için 512 KB SRAM ile sınırlıdır. Konuşmadan metne çevirme, dil modeli çıkarımı ve ses sentezi gibi modern yapay zekâ işlemleri için bu kaynak yetersizdir. Bu nedenle ESP32 sadece **algılayıcı, framing yapan ve aktüatör süren** bir edge cihaz olarak konumlandırılmıştır. Bilgisayar tarafı ise Whisper + Llama + Piper üçlüsünü çalıştırarak gerçek anlamda yapay zekâ beyni rolünü üstlenir.

Bu ayrım hocanın örnek raporundaki **"runs on the ESP32-S3 or local PC"** ifadesi ile bire bir örtüşür; bilgisayar bir edge gateway gibi davranır, internet asla devreye girmez.

İkinci önemli felsefe **MVP-önce, iyileştirme-sonra** yaklaşımıdır. Saf LLM ile çalışan ve sadece butonla tetiklenen bir prototip önce inşa edilir. Wake word katmanı (Edge Impulse ile özel eğitilmiş model) projenin son aşamasına bırakılır; bu sayede en uzun süren araştırma adımının aksamaya uğraması durumunda bile MVP demo edilebilir kalır.

Üçüncü felsefe **dürüstlük ve pragmatizm**dir. Donanım kısıtları (PSRAM eksikliği, level shifter sorunu, güç bütçesi sınırı), yazılım kısıtları (ESP-IDF öğrenme eğrisi) ve etik boyut raporda doğrudan ele alınır; "gümrük altına alınan" tasarım kararları gizlenmez.

---

## 3. Sistem Mimarisi

Sistem üç ana katmandan oluşur.

**Saha cihazı katmanı** ESP32-S3-DevKitM-1-N8 etrafında inşa edilmiştir. Bu katmanın görevleri mikrofon dinleme, buton durumu izleme, sensör okuma, sürekli durum gösterimi ve hoparlör çıkışını yönetmektir. İleri aşamada wake word algılama da bu katmana eklenecektir. Tüm bu işler real-time olarak yürütülür ve karmaşık karar mekanizmaları içermez.

**USB-Serial köprü katmanı** ESP32-S3'ün dahili USB-OTG portu üzerinden çalışan CDC (Communications Device Class) seri haberleşmesini kullanır. Çift yönlü binary protokol ile ses parçaları, sensör okumaları, durum mesajları ve komutlar bu hat üzerinden taşınır. CRC16 doğrulama ile veri bütünlüğü garanti altına alınır.

**Yapay zekâ beyin katmanı** kullanıcının kişisel bilgisayarında Python uygulaması olarak çalışır. Faster-Whisper modeli sesi metne çevirir, Ollama üzerinden çalışan Llama 3.2 3B modeli metni ve sensör verisini değerlendirir, Piper TTS ürettiği cevabı sese dönüştürür. Tüm modeller önceden indirilmiş haldedir ve internet bağlantısı olmadan çalışır.

Veri akışı şu sırayla ilerler: kullanıcı butona basar → ESP32 LED'leri kırmızı dönüşe alır, mikrofon parçalarını PC'ye akıtmaya başlar → eş zamanlı olarak MAX30102 sensör verisi gönderilir → kullanıcı butonu bırakınca ESP32 "kayıt bitti" sinyalini gönderir → Whisper sesi metne çevirir → Llama metni ve sensör verisini birlikte değerlendirir → ürettiği cevap Piper ile seslendirilir → ses parçaları ESP32'ye gönderilir → MAX98357 amfi üzerinden hoparlörden çalınır. Tüm bu süreç boyunca NeoPixel halka her aşamada farklı renk ve animasyon ile durumu görsel olarak bildirir.

---

## 4. Geliştirme Platformu: ESP-IDF

Hocanın izniyle proje **Arduino IDE yerine ESP-IDF üzerinde** geliştirilecektir. Geliştirme ortamı **VS Code + ESP-IDF Extension** olacak. Bu seçimin gerekçeleri ve dikkat edilmesi gereken konular aşağıda detaylandırılmıştır.

### ESP-IDF'in Bu Proje İçin Avantajları

ESP-IDF, Espressif'in ESP32-S3 için resmi geliştirme framework'üdür. ESP32-S3'ün modern donanım özelliklerine (USB-OTG, USB-CDC, çift I2S, RMT, dahili JTAG) en eksiksiz erişim ESP-IDF üzerindendir. FreeRTOS native olarak kullanılır; bu sayede projemizin doğal yapısı (mikrofon kayıt task'ı, buton izleme task'ı, USB iletişim task'ı, LED animasyon task'ı paralel çalışmalı) doğrudan modellenir.

ESP-IDF'in Component Manager altyapısı sayesinde Espressif'in resmi component'lerine erişim sağlanır: `espressif/led_strip` NeoPixel için RMT tabanlı sürücü sunar (Adafruit NeoPixel'den daha verimli), `espressif/esp-tflite-micro` TensorFlow Lite Micro entegrasyonu için resmi destekli porttur, USB-CDC desteği framework'ün içine gömülüdür.

### Bilmemiz Gereken Zorluklar

ESP-IDF'in Arduino IDE'den **dik bir öğrenme eğrisi** vardır. CMake build sistemi, component yapısı, menuconfig konfigürasyonu, idf.py komut zinciri başta yabancı gelir. İlk hafta belki bir-iki güne mal olan oryantasyon süresi normaldir.

**MAX30102 sensörü için resmi/yaygın bir ESP-IDF kütüphanesi yoktur.** Arduino tarafında SparkFun MAX3010x kütüphanesi standart çözümdür, ESP-IDF'de doğrudan karşılığı yoktur. İki çözüm yolu vardır:

- **Arduino-as-component yaklaşımı:** ESP-IDF projesi içinde Arduino kütüphanelerini de kullanmaya izin verir. SparkFun MAX3010x bu yolla doğrudan kullanılabilir. Bu en pragmatik yoldur ve önerilen yaklaşımdır.
- **Sıfırdan C portu:** I²C register'larını doğrudan ESP-IDF'in `driver/i2c.h` API'si ile yönetip Maxim'in referans algoritmasını C'ye çevirmek. Daha temiz ama zaman alır.

Build süreleri Arduino'dan biraz daha uzundur (ilk derleme 1-2 dakika). Debug çıktısı `ESP_LOGI` makroları üzerinden gider, Arduino'nun `Serial.println` kadar minimalist değildir ama daha güçlüdür (log seviyeleri, renkli çıktı, tag bazlı filtreleme).

### Hafifletme Stratejisi

Geliştirmeye başlamadan önce **bir hafta hazırlık dönemi** ayrılması önerilir: VS Code + ESP-IDF Extension kurulumu, basit blink LED örneği, USB-CDC hello-world testi. Bu hazırlık aşamasından sonra Aşama 1'e geçilir.

---

## 5. Donanım Bileşenleri

Aşağıdaki tablo projede kullanılan bütün donanımı göstermektedir.

| Bileşen | Model | Sistemdeki Rolü |
|---|---|---|
| Geliştirme kartı | ESP32-S3-DevKitM-1-N8 (PSRAM yok, 8 MB flash) | Sistem yöneticisi, edge işlemci |
| Mikrofon | INMP441 (I2S, MEMS, 24-bit ΔΣ ADC) | Kullanıcı sesini yakalayan giriş |
| Ses yükseltici | MAX98357A (I2S Class-D, mono) | Hoparlöre güç sağlayan amfi |
| Hoparlör | 4Ω, 3W, 40mm dinamik | Yapay zekâ cevabını duyuran çıkış |
| Vital sensör | MAX30102 (I²C, 18-bit ADC, kırmızı+IR LED) | Nabız ve SpO₂ ölçer |
| Durum göstergesi | WS2812B 8-LED NeoPixel halka | Sistem durumunu renkli olarak gösterir |
| Mantık seviyesi dönüştürücü | BSS138 4-kanal MOSFET tabanlı | I²C/yavaş dijital sinyaller için (NeoPixel için DEĞİL — aşağıya bkz.) |
| Push butonu | Anlık, normalde açık | Push-to-talk tetikleyici |
| Schottky diyot (opsiyonel) | 1N5817 veya benzeri | NeoPixel beslemesini ~4.3V'a düşürmek için |
| Filtre kondansatörü | 100-470 µF elektrolitik + 100 nF seramik | Güç hattı stabilizasyonu |
| Breadboard ve jumper kablolar | Standart | Prototip kurulumu |

Donanım seçimlerinin teknik gerekçeleri donanım raporunda detaylandırılmıştır; burada projeye özel olanlar vurgulanır.

**INMP441 mikrofon** I2S üzerinden doğrudan dijital ses gönderir; analog ADC kalitesi ile sınırlanmaz. 24-bit ΔΣ ADC içerir, SNR 61 dBA'dır. Sleep modunda 1 µA'ya kadar düşer. Sadece I2S slave olarak çalışır, MCLK gerektirmez.

**MAX98357A amfi** I2S DAC ve Class-D amplifikatörü tek bir entegrede birleştirir. MCLK gerektirmez (PLL ile BCLK'den üretir). Çıkışı BTL (Bridge-Tied Load); hoparlörün iki ucu da amfiye gider, hiçbir ucu GND'ye bağlanmaz. 5V besleme ile 4Ω hoparlörde 3.2W gücüne ulaşır, %92 verimle çalışır.

**MAX30102 sensörü** PPG (fotopletismografi) tekniği ile çalışır. Kırmızı (660 nm) ve kızılötesi (880 nm) LED'lerden dokuya gönderilen ışığın yansıması fotodiyot ile ölçülür. Nabız modülasyonundan BPM, AC/DC oranlarından SpO₂ hesaplanır. I²C üzerinden 0x57 sabit adresiyle konuşur; dahili FIFO 32 örnek derinliktedir. INT pini açık-drain aktif-LOW kesme çıkışıdır.

**WS2812B NeoPixel halkası** her bir LED'in 24-bit (GRB sırasıyla) bilgisini NRZ tabanlı tek hatla aldığı adreslenebilir LED matrisidir. 8 LED'lik bir halkadır. Veri hızı 800 kbps; bu yüksek hız level shifter seçiminde sorun yaratır (aşağıya bkz.). Tek bir LED tam beyaz parlaklıkta yaklaşık 60 mA çeker; 8 LED toplam tam beyaz akımı ~480 mA tepe değerine ulaşır.

### Kritik Donanım Uyarısı: Level Shifter Sorunu

Elimizdeki **BSS138 tabanlı 4-kanal level shifter modülü WS2812B için uygun DEĞİLDİR.** Sebebi: modülün her kanalında 10 kΩ pull-up direnci vardır ve bu pull-up + hat kapasitansının RC zaman sabiti, 800 kbps NRZ sinyalinin gerektirdiği keskin yükselen kenarları (~0.4 µs HIGH süresi) tutturamaz. Pratik sonuç: LED'ler titrer, yanlış renkler gösterir, veya hiç tepki vermez. Bu modül **I²C, yavaş SPI veya UART** gibi yavaş veya açık-drain dijital sinyaller için saklanmalıdır.

WS2812B için üç pratik çözüm vardır:

1. **Doğrudan 3.3V sürme:** ESP32 GPIO'sundan WS2812B'nin DIN pinine doğrudan bağlantı. Datasheet V_IH için 5V besleme altında 3.5V ister; ESP32'nin 3.3V çıkışı bu eşiğin altındadır. Pratikte birçok parti hoşgörür ama kararsızdır. İlk LED'in titrediği veya yanlış renk gösterdiği durumlar yaygındır.

2. **Schottky diyot ile besleme indirme:** WS2812B'nin 5V hattına seri olarak bir Schottky diyot (1N5817 veya benzeri) koymak besleme gerilimini ~4.3V'a düşürür. V_IH eşiği bu durumda 3.0V'a iner ve ESP32'nin 3.3V çıkışı geçerli HIGH olarak okunur. Hızlı ve ucuz çözüm.

3. **74AHCT125 veya SN74LVC1T45:** Doğru level shifter IC'leri. Tek yönlü, 5V besleme + 3.3V girişi geçerli HIGH olarak algılayan tampon entegreler. En sağlam çözüm ama satın alma gerektirir.

**Proje yaklaşımı:** Aşama 2'de doğrudan sürme denenir. LED'ler kararsız çalışırsa Schottky diyot ile geçici çözüm uygulanır. Demo gününde gerekirse 74AHCT125 alınır.

### Güç Bütçesi

Tam yükte sistem akım çekişi USB'nin tipik limitini aşabilir:

| Yük | Tepe Akımı |
|---|---|
| ESP32-S3 + sensörler + mikrofon | ~150 mA |
| MAX98357A (yüksek seste) | ~600 mA |
| 8-LED NeoPixel (tam beyaz) | ~480 mA |
| **Toplam (tepe)** | **~1.2 A** |
| Tipik USB PC portu | ~500 mA |

Bu farkı kapatmak için aşağıdaki stratejiler uygulanır:

- NeoPixel parlaklığı yazılımsal olarak %25-30 ile sınırlanır (~120 mA'ya düşer)
- Hoparlör seviyesi yazılımsal olarak %80 ile sınırlanır
- Tam parlaklık efektleri (örneğin "ERROR" durumunda kırmızı flash) çok kısa sürelidir
- Demo sırasında PC'ye USB ile bağlı kalmakla birlikte, isteğe bağlı olarak harici **5V/2A adaptör** kullanılabilir (yedek olarak hazır bulundurulur)

Bu uygulamada NeoPixel ve MAX98357A'nın 5V güç hattı doğrudan kart üzerindeki **VBUS pinine** bağlanır; dahili 3.3V regülatörü bu yükü taşımaz.

---

## 6. Pin Şeması

Pin atamaları, donanım raporundaki Espressif/SparkFun referansları ve ESP32-S3-DevKitM-1'in strapping kısıtları dikkate alınarak belirlenmiştir.

| Bileşen Ucu | ESP32-S3 Pin | Açıklama |
|---|---|---|
| INMP441 VDD | 3.3V | Mikrofon beslemesi |
| INMP441 GND | GND | Ortak toprak |
| INMP441 L/R | GND | Sol kanal seçimi (asla boşta bırakılmaz) |
| INMP441 SD | GPIO 2 | I2S0 ses verisi (mic → ESP32) |
| INMP441 SCK | GPIO 41 | I2S0 bit saati |
| INMP441 WS | GPIO 42 | I2S0 kelime seçimi |
| MAX98357A VIN | 5V (VBUS) | Amfi beslemesi |
| MAX98357A GND | GND | Ortak toprak |
| MAX98357A DIN | GPIO 7 | I2S1 ses verisi (ESP32 → amfi) |
| MAX98357A BCLK | GPIO 5 | I2S1 bit saati |
| MAX98357A LRC | GPIO 6 | I2S1 kelime seçimi |
| MAX98357A SD (mute) | GPIO 10 | Soft-mute kontrolü (opsiyonel) |
| MAX98357A GAIN | Boşta | 9 dB kazanç (varsayılan) |
| MAX98357A + / − | Hoparlör | BTL çıkışı (her iki uç hoparlöre) |
| MAX30102 VIN | 3.3V | Sensör beslemesi |
| MAX30102 GND | GND | Ortak toprak |
| MAX30102 SDA | GPIO 8 | I²C veri |
| MAX30102 SCL | GPIO 9 | I²C saat |
| MAX30102 INT | GPIO 4 | Yeni veri kesmesi |
| Push buton | GPIO 21 | Dahili pull-up, basınca LOW |
| NeoPixel 5V | 5V (VBUS) | LED beslemesi |
| NeoPixel GND | GND | Ortak toprak |
| NeoPixel DIN | GPIO 38 | RMT tabanlı veri (level shifter notuna bkz.) |
| 100-470 µF kondansatör | NeoPixel 5V ↔ GND | Güç hattı stabilizasyonu |
| 220-470 Ω direnç | GPIO 38 ↔ NeoPixel DIN | Yansıma koruması |

Pin seçim gerekçeleri:

- **GPIO 21 buton için seçildi** çünkü ESP32-S3'te boot sırasında strapping pin değildir ve RTC-capable'dır (deep sleep'ten uyandırma için kullanılabilir, gelecek özellik).
- **GPIO 2, 41, 42 INMP441 için** donanım raporundaki Espressif/SparkFun önerisidir.
- **GPIO 5, 6, 7 MAX98357A için** yine donanım raporundaki referansa dayanır.
- **GPIO 38 NeoPixel için** RMT periferinin kolay erişimli bir GPIO'sudur ve strapping değildir.
- **GPIO 10 MAX98357A mute için** opsiyoneldir; eklenirse hoparlörün konuşmadığı zamanlarda LOW çekilerek "5 saniye sonra otomatik shutdown click" sesi önlenir.
- **GPIO 4 MAX30102 INT için** boş ve uygun bir pindir.

NeoPixel'in 5V besleme hattı kartın USB VBUS'ından alınır, 3.3V regülatöründen değil. Bu, hem regülatörü korur hem de PC USB portuna doğrudan yük dağıtır.

### Yıldız Topraklama Notu

MAX98357A Class-D çıkışı yüksek anlık akım çeker (1A üstü tepe). Tüm GND'lerin **yıldız (star) noktasında** birleştirilmesi önemlidir; aksi halde dönüş akımı INMP441'in analog hattından geçerek "whine" benzeri yüksek frekanslı gürültü oluşturur. Pratikte breadboard'da GND raylarını tek bir merkez noktadan ESP32 GND pinine bağlamak yeterlidir.

---

## 7. Yazılım Yığını

### Ne Kullanıyoruz, Neden?

**ESP32 tarafı**, ESP-IDF v5.x framework'ü üzerinde geliştirilecektir. Kullanılacak ana bileşenler:

- **espressif/led_strip**: Resmi NeoPixel sürücüsü, RMT periferiyi kullanır
- **espressif/esp-tflite-micro**: TensorFlow Lite Micro'nun ESP-IDF için resmi portu (wake word için Aşama 11'de)
- **driver/i2s_std**: ESP32-S3'ün yeni I2S standart sürücüsü, hem RX hem TX modunu destekler
- **driver/i2c_master**: Standart I²C master sürücüsü (MAX30102 için)
- **driver/gpio**: Buton girişi için
- **tinyusb**: USB-CDC için (Espressif'in TinyUSB portu, native ESP-IDF desteği)
- **SparkFun MAX3010x** (Arduino kütüphanesi): Arduino-as-component yaklaşımı ile MAX30102 sensör mantığı için kullanılır

**PC tarafı**, Python 3.10 veya üzeri ile geliştirilecektir.

#### Faster-Whisper (sesi metne çeviren modül)

OpenAI'nin Whisper modeli, 2022'de açıklanan ve 99+ dili destekleyen otomatik konuşma tanıma sistemidir. **Faster-Whisper** ise bu modelin **CTranslate2 backend'i ile yeniden yazılmış optimize versiyonudur** — orijinal Whisper'a göre yaklaşık 4 kat hızlı çalışır, bellek kullanımı yarıya düşer.

Bizim kullanacağımız model: **small.en (480 MB)**. Sonundaki `.en` sadece İngilizce için optimize edilmiş sürüm olduğunu gösterir; çok dilli sürümden hızlı ve daha doğrudur. RTX 3050 Ti üzerinde tipik bir 5 saniyelik kayıtı ~0.5 saniyede metne çevirir. Tamamen offline çalışır; model dosyası bir kez indirilir (`huggingface.co/Systran/faster-whisper-small.en`), sonra internet gerektirmez.

#### Llama 3.2 3B (karar veren modül)

Meta tarafından açık kaynak olarak yayınlanan dil modeli ailesinin 3 milyar parametreli üyesi. Sınıflandırma ve kısa metin üretimi için yetkindir, daha büyük modellere göre çok hızlıdır. **Ollama** isimli yerel LLM çalıştırma altyapısı üzerinden kullanılacaktır:

- Kurulum tek komuttur: `curl -fsSL https://ollama.com/install.sh | sh`
- Model indirme: `ollama pull llama3.2:3b` (Q4_K_M kuantizasyonu, ~2 GB)
- Çalıştırma: arka planda HTTP servisi açar, Python `requests.post()` ile çağırılır
- VRAM kullanımı RTX 3050 Ti üzerinde ~2.5 GB

Sistem promptu modelin rolünü, çıktı formatını ve sınırlarını belirleyecektir; bu prompt geliştirme aşamasında ayrıca yazılacaktır.

#### Piper TTS (metni sese çeviren modül)

Rhasspy projesinin geliştiricileri tarafından üretilen açık kaynak **Text-to-Speech** motoru. Tamamen offline çalışır, CPU üzerinde gerçek zamandan hızlıdır. Bizim kullanacağımız model: **en_US-amy-medium** (yaklaşık 60 MB) — orta kaliteli, doğal tınılı bir İngilizce kadın sesidir. Python kütüphanesi `pip install piper-tts` ile kurulur, model dosyası ayrı indirilir.

Piper streaming destekler; LLM'in ürettiği metin parça parça sentezlenip ESP32'ye akıtılabilir, kullanıcı ilk kelimeyi duymak için tüm cümlenin sentezini beklemez.

#### USB Köprüsü

`pyserial` kütüphanesi standart USB-Serial haberleşmesi için kullanılır. Bizim binary protokolümüz bu kütüphanenin üstüne yazılır.

### Bilgisayar Donanım Yeterliliği

Sistem yığınının RTX 3050 Ti (4 GB VRAM) + 16 GB DDR4 + Ryzen 7 5600H konfigürasyonu için VRAM dağılımı:

| Modül | Yaklaşık VRAM |
|---|---|
| Faster-Whisper small.en | ~500 MB |
| Llama 3.2 3B (Q4_K_M) | ~2.5 GB |
| Piper TTS | CPU (~100 MB RAM) |
| Diğer Python süreçleri | ~200 MB |
| **Toplam VRAM kullanımı** | **~3.2 GB** |
| **Boşta kalan** | **~800 MB** |

Marj sınırdadır ama yeterlidir.

---

## 8. Çalışma Akışı

Sistem akışı durum makinesi olarak modellenir.

**IDLE (Boşta):** Sistem başlangıç ve dinlenme halidir. ESP32 buton durumunu sürekli izler. NeoPixel halka çok yumuşak yeşil nefes animasyonu ile kullanıcıya hazır olduğunu hissettirir. PC ile aktif veri alışverişi yoktur, sadece saniyede bir heartbeat mesajı atılır. **Wake word aşaması eklendiğinde** mikrofon dinlemesi de bu durumda paralel olarak başlar.

**RECORDING (Kaydediyor):** Butona basıldığında girilen durumdur. NeoPixel kısa bir mavi parlama yapar, sonra saat yönünde dönen kırmızı animasyona geçer. ESP32 her 20-30 milisaniyede bir 512 byte'lık ses parçası gönderir, eş zamanlı olarak MAX30102 sensöründen periyodik nabız ve SpO₂ okumaları akıtılır. Buton bırakıldığında bu durum sonlandırılır. **Wake word katmanı eklendiğinde** wake word tetiklemesi durumunda 6 saniyelik bir timeout ya da kullanıcının "STOP" butonuna basması bu durumu sonlandırır.

**PROCESSING (İşleniyor):** PC'nin sırasıyla Whisper konuşma tanımayı, Llama 3.2 dil modeli çıkarımını ve Piper ses sentezini yürüttüğü aşamadır. ESP32 bu sırada sadece bekler ve NeoPixel halkayı yavaş mor pulsasyon animasyonu ile sürer. Tipik olarak 2-5 saniye sürer.

**SPEAKING (Konuşuyor):** PC üretilen ses parçalarını ESP32'ye gönderir, ESP32 bunları MAX98357A amfi üzerinden hoparlöre yönlendirir. NeoPixel halka sabit yeşil yanar. Mikrofon kaydı pasif tutulur (echo loop önlemi). MAX98357A'nın SD pini bu durumda HIGH çekilir; bittikten sonra tekrar LOW çekilerek otomatik shutdown click'i önlenir. Konuşma bittiğinde sistem yeniden IDLE'a döner.

**ERROR (Hata):** Herhangi bir alt sistemde hata oluştuğunda girilir (USB bağlantı kopukluğu, sensör okuma hatası, modelin yanıt verememesi vs.). NeoPixel halka üç kez kırmızı flaş yapar, sonra IDLE'a döner. Hata kodu seri porta yazılır.

---

## 9. Tetikleme: Buton (MVP) ve Wake Word (Sonraki Aşama)

Sistem **iki aşamalı** geliştirilecektir.

### Aşama A: Buton ile MVP

MVP'de tek tetikleme yolu vardır: push-to-talk butonu. Bu **bas-tut (push-and-hold)** davranışıyla çalışır — kullanıcı butonu bastığı sürece kayıt devam eder, bıraktığında biter. Bu davranış üç açıdan en uygun seçimdir: en sezgiseldir (kullanıcı "konuşmaya başlıyorum, basıyorum; bitirdim, bırakıyorum" diye düşünür), en güvenilirdir (yanlışlıkla iki kere basma veya unutma riski yok), ve test edilmesi en kolayıdır.

Buton GPIO 21 ile GND arasına bağlanır; ESP32'nin dahili pull-up direnci aktive edilir. Buton basıldığında pin LOW, serbestken HIGH okur. Yazılımda 20 milisaniyelik debounce filtresi uygulanır.

### Aşama B: Wake Word Eklenmesi (Son Aşamada)

MVP çalıştıktan ve doğrulandıktan sonra, **Edge Impulse** platformu üzerinde "Emergency" kelimesi için özel bir TFLite Micro modeli eğitilir. Edge Impulse, gömülü cihazlar için makine öğrenmesi modelleri üreten web tabanlı bir platformdur, ücretsiz hesap sunar. Eğitim adımları:

1. "Emergency" örnekleri toplanır (kendi sesimiz, arkadaşlardan, mümkünse farklı aksanlarda) — hedef 200-300 örnek
2. "Noise" sınıfı için arka plan sesleri eklenir (klima, klavye, başka konuşmalar)
3. Web arayüzünde model eğitilir (yaklaşık 30-60 dakika)
4. Sonuç: ~50-100 KB boyutunda TFLite model — ESP32 SRAM'ine sığar
5. ESP-IDF projesine `espressif/esp-tflite-micro` ile entegre edilir

Wake word ile tetikleme sonrasında kayıt iki yoldan biriyle sonlandırılır: ya **kullanıcı butona basarak** (manuel sonlandırma) ya da **6 saniye sonra otomatik** (timeout). Sessizlik tabanlı (VAD) sonlandırma kullanılmayacaktır; sistem basit ve öngörülebilir tutulur.

Hedef metrikler: wake word doğru tanıma oranı %85+, false-positive oranı %5'in altında.

---

## 10. PC İşleme Hattı

Ses kaydı tamamlandığında PC tarafında üç aşamalı bir işlem hattı çalışır.

**Birinci aşama (Faster-Whisper konuşma tanıma):** ESP32'den gelen tüm ses parçaları int16 PCM tamponunda birleştirilir ve faster-whisper modeline verilir. Çıktı kullanıcının söylediklerinin İngilizce metnidir.

**İkinci aşama (Llama 3.2 yanıt üretimi):** Whisper'ın çıkardığı metin, MAX30102 sensöründen alınan son nabız ve SpO₂ değerleri ve sistem promptu Ollama'ya gönderilir. Ollama, Llama 3.2 3B modelini çağırır ve İngilizce ilk yardım talimatı üretir. Sistem promptu modelin rolünü, çıktı formatını ve sınırlarını net olarak belirler.

**Üçüncü aşama (Piper ses sentezi):** Llama'nın ürettiği metin Piper TTS'e gönderilir. Piper, metni doğrudan 16 kHz örnekleme hızında int16 PCM ses akışına dönüştürür. Çıktı **parçalar halinde** alınır ve hemen ESP32'ye akıtılır; tüm metnin sentezi beklenmeden ilk kelime kullanıcıya ulaşır.

---

## 11. USB-Serial Protokolü

ESP32 ile PC arasındaki haberleşme **binary çerçeveli protokol** kullanır. Çerçeve yapısı:

```
[0xAA 0x55] [LEN: 2 byte BE] [TYPE: 1 byte] [PAYLOAD: LEN byte] [CRC16: 2 byte]
```

Sync word (0xAA 0x55) alıcının buffer'da kayma olursa senkronizasyonu yeniden kurmasını sağlar. CRC16 (CCITT polinomu, 0x1021) header + payload üzerinde hesaplanır.

Mesaj tipleri:

| Type | Yön | Adı | Payload |
|---|---|---|---|
| 0x01 | ESP→PC | AUDIO_UP | int16 PCM (16 kHz, mono) |
| 0x02 | ESP→PC | SENSOR | HR (u16), SpO₂ (u8), valid (u8) |
| 0x03 | ESP→PC | BUTTON | event (u8: 0=release, 1=press) |
| 0x04 | ESP→PC | STATUS | state (u8), error_code (u8) |
| 0x05 | ESP→PC | WAKE_DETECTED | confidence (u8) — Aşama B'de |
| 0x06 | ESP→PC | HEARTBEAT | uptime (u32) |
| 0x10 | PC→ESP | AUDIO_DOWN | int16 PCM (16 kHz, mono) |
| 0x11 | PC→ESP | LED | pattern_id (u8), brightness (u8) |
| 0x12 | PC→ESP | PLAYBACK_END | (yok) |
| 0x13 | PC→ESP | RESET | (yok) |

Ses parçaları için tipik parça boyutu 512 byte'tır (~16 ms ses). Bant genişliği yaklaşık 256 kbps; USB-CDC bunu rahatlıkla taşır.

---

## 12. NeoPixel Durum Göstergesi

| Durum | Animasyon |
|---|---|
| IDLE | Yumuşak yeşil nefes, 4 saniyede bir tam dönüş, parlaklık %5-15 |
| BUTTON_PRESSED | 200 ms mavi parlama, sonra RECORDING'e geçer |
| WAKE_DETECTED (Aşama B) | 200 ms beyaz parlama, sonra RECORDING'e geçer |
| RECORDING | Saat yönünde dönen kırmızı LED, saniyede bir tur |
| PROCESSING | Yavaş mor pulsasyon, 2 saniyede döngü |
| SPEAKING | Sabit yeşil, %80 parlaklık |
| ERROR | Üç kırmızı flaş, sonra IDLE'a döner |

Halka parlaklığı varsayılan **%30** (~80/255) tutulur. Bu hem akım çekişini ~140 mA'ya indirir hem göze rahat gelir. Tam beyaz/tam parlaklık efekti sadece çok kısa "flash" anlarında kullanılır.

---

## 13. Bilgi Tabanı Felsefesi: Saf LLM ile Başla

MVP saf LLM yaklaşımıyla geliştirilir: kullanıcının söylediği metin doğrudan modele gider, model kendi muhakemesiyle yanıt üretir, üretilen yanıt seslendirilir.

Bu kararın gerekçeleri: bilgi tabanı tabanlı hibrit yapı (klasifikasyon + sabit metinler + LLM ile kişiselleştirme) teorik olarak daha denetlenebilir ama iyi tasarlanmış bir sistem promptu zaten dil modelini sınırlı bir role oturtmaktadır. Hibrit yapının asıl faydası çok geniş bilgi tabanı (50+ kategori), katı determinizm veya tıbbi audit gereği gibi spesifik durumlarda öne çıkar — bu proje akademik bir gösteri olarak bu üçünü gerektirmez.

Eğer projenin son haftalarında zaman kalırsa **RAG (Retrieval-Augmented Generation)** katmanı raporun "Future Work" bölümüne taşınır.

Sistem promptu uluslararası kılavuzlara (American Heart Association, American Red Cross, World Health Organization Basic Emergency Care, European Resuscitation Council) atıfla yazılacaktır. Promptun nihai metni geliştirme aşamasında ayrıca düzenlenecek ve test edilerek iyileştirilecektir.

---

## 14. Etik ve Güvenlik Yaklaşımı

Bu proje **akademik bir ödev ve gösteri sistemi**dir. Gerçek tıbbi cihaz değildir, klinik kullanım amaçlamaz, sertifikalı bir sağlık ürünü olarak kullanılması düşünülemez. Bu çerçevede etik yaklaşım dengeli tutulur: aşırı muhafazakar olmak sistemin işlevsiz kalmasına yol açar; aşırı serbest olmak ise sınıf dışında biri sistemi gerçekten ciddiye alırsa potansiyel zarar doğurabilir.

### Modelin Yapabilecekleri

- Genel ilk yardım müdahalelerini önerebilir (örneğin Heimlich manevrası adımları, CPR sıkıştırma sayısı, kanama bastırma teknikleri)
- Sensör verisini yorumlayıp duruma uygun ilk müdahaleyi seçebilir (örneğin düşük SpO₂ durumunda hava yolunu açmaya öncelik verir)
- Genel kategorideki ilaçlardan bahsedebilir (örneğin "ağrı kesici alabilir" veya "şüphe halinde antihistaminik düşünülebilir")
- Acil durum çağrısı yapılmasını net olarak önerir

### Modelin Yapmayacakları

- **Spesifik ilaç dozajı önermez** (örneğin "200mg ibuprofen al, 6 saatte bir tekrarla" gibi nicel reçeteler yasaktır)
- **Klinik teşhis koymaz** ("Kalp krizi geçiriyor" gibi kesin tanılar yerine "kalp krizi belirtileri olabilir, derhal yardım çağırın" tarzı yaklaşır)
- **Tıbbi prosedür önermez** (cerrahi müdahale, enjeksiyon, ilaç kombinasyonu gibi profesyonel müdahaleler hakkında konuşmaz)

### Zorunlu Uyarı

Her ses çıktısının sonunda şu uyarı yer alır (sistem promptu bunu zorlar):

> *"This is general first-aid information, not a medical diagnosis. Call emergency services immediately."*

### Dokümantasyon Boyutu

README dosyasında ve raporda sistemin **eğitim ve akademik gösteri amaçlı** olduğu açıkça belirtilir; gerçek bir acil durumda profesyonel sağlık hizmetine başvurulması vurgulanır.

---

## 15. Wokwi Simülasyon Stratejisi

Hocanın yönergesi Wokwi tabanlı devre diyagramı zorunlu kılar. Wokwi'de elimizdeki tüm bileşenler birebir yoktur; bu durum dürüstçe raporda açıklanır.

**ESP32-S3-DevKitM-1 Wokwi'de doğrudan mevcut değildir**, ancak **DevKitC-1** vardır. İki kartın GPIO pin atamaları aynıdır ve yazdığımız kod her ikisinde de değişmeden çalışır. Raporda şu cümleyle geçilir: *"The Wokwi simulation uses the ESP32-S3-DevKitC-1 model since DevKitM-1 is not available in the simulator. Pin assignments and code are identical; only the physical form factor differs."*

Wokwi'de **mevcut bileşenler:** ESP32-S3-DevKitC-1, WS2812B NeoPixel halka, MAX30102 sensörü, push butonu, dirençler, kondansatörler.

Wokwi'de **mevcut olmayan bileşenler:** INMP441 (jenerik mikrofon sembolü ile temsil edilir), MAX98357A amfi ve hoparlör (jenerik hoparlör sembolü ile temsil edilir), seviye dönüştürücü (Wokwi ideal voltaj seviyesi simüle eder).

Wokwi diyagramı bu durumda **şematik temsil** rolü üstlenir: pin bağlantılarını, güç dağılımını ve genel topolojiyi gösterir. Gerçek demo sınıfta fiziksel donanım ile yapılacağı için bu durum kabul edilebilir.

---

## 16. Klasör Yapısı

```
CEN322_Project_YourName_YourSurname/
├── firmware/                           # ESP-IDF projesi
│   ├── CMakeLists.txt
│   ├── sdkconfig                       # ESP-IDF konfigürasyonu
│   ├── main/
│   │   ├── CMakeLists.txt
│   │   ├── main.c                      # app_main(), task'lar
│   │   ├── audio_input.c               # I2S0 INMP441
│   │   ├── audio_output.c              # I2S1 MAX98357A
│   │   ├── sensor.c                    # I²C MAX30102 sürücü
│   │   ├── button.c                    # GPIO buton + debounce
│   │   ├── led_strip.c                 # RMT NeoPixel
│   │   ├── usb_cdc.c                   # USB-CDC haberleşmesi
│   │   ├── protocol.c                  # Frame oluşturma/ayrıştırma
│   │   ├── wake_word.c                 # Aşama B
│   │   └── config.h
│   └── components/                     # Arduino-as-component (gerekirse)
│       └── arduino/
├── pc/                                 # Python orchestrator
│   ├── main.py
│   ├── serial_bridge.py
│   ├── stt.py                          # faster-whisper sarmalayıcı
│   ├── llm.py                          # Ollama HTTP istemcisi
│   ├── tts.py                          # piper-tts sarmalayıcı
│   ├── system_prompt.txt
│   └── requirements.txt
├── wokwi/
│   ├── diagram.json
│   └── project_link.txt
├── docs/
│   ├── protocol_specification.md
│   ├── pin_mapping.md
│   ├── hardware_report.md              # Mevcut donanım raporu
│   └── final_report.docx
├── wake_word_training/                 # Edge Impulse projesi (Aşama B'de)
│   ├── samples/
│   ├── trained_model.tflite
│   └── notes.md
└── README.md
```

Final teslimde aşağıdaki dosyalar paketlenir: rapor docx, firmware/ klasörü (ESP-IDF projesi), pc/ klasörü, wokwi/ klasörünün içeriği, README.md.

---

## 17. Geliştirme Yol Haritası

Yol haritası **buton ile MVP, wake word'e en sonda** yaklaşımıyla yeniden düzenlenmiştir.

| Aşama | Açıklama | Çıktı |
|---|---|---|
| 0 | VS Code + ESP-IDF Extension kurulumu, blink LED örneği, USB-CDC hello-world testi | ESP-IDF ortamına aşinalık |
| 1 | USB-Serial echo testi (ESP32 ↔ Python) | Temel iletişim hattı doğrulanır |
| 2 | NeoPixel halka çalıştırma, durum animasyonları | Görsel geri bildirim altyapısı + level shifter sorunu test edilir |
| 3 | Push-to-talk butonu, debounce, durum animasyonu ile entegrasyon | Buton mantığı tam test edilir |
| 4 | MAX30102 I²C üzerinden okuma, nabız ve SpO₂ değerleri | Sensör çalışır halde |
| 5 | INMP441 I2S kayıt, ses verisi PC'ye akış | Ses kalitesi doğrulanır (Audacity ile) |
| 6 | MAX98357A I2S çıkış, PC'den ses çalma | Hoparlör çalışır halde |
| 7 | Binary frame protokolü, CRC16 doğrulama | Tüm haberleşme framed protokol üzerinden |
| 8 | PC tarafında faster-whisper kurulumu, statik ses dosyası transkripsiyon | STT çalışır halde |
| 9 | Ollama + Llama 3.2 3B kurulum, sistem prompt v1 yazımı | LLM yanıt üretir |
| 10 | Piper TTS kurulumu, metin → ses üretimi | TTS çalışır halde |
| 11 | **İlk uçtan uca demo: buton → kayıt → STT → LLM → TTS → çal** | **MVP HAZIR** |
| 12 | Edge Impulse ile wake word veri toplama ve model eğitimi | TFLite model elde edilir |
| 13 | esp-tflite-micro entegrasyonu, wake word algılama firmware'e eklenir | Wake word çalışır halde |
| 14 | Sistem optimizasyonu, prompt iyileştirme, demo testi | Cilalanmış MVP |
| 15 | Rapor yazımı, Wokwi diyagramı, README hazırlığı | Teslim hazır |

**Kritik milestone Aşama 11'dir** — burada zaten gösterilebilir bir sistem oluşmuş demektir. Sonraki aşamalar iyileştirme ve cilalama niteliğindedir. Wake word (Aşama 12-13) en uzun süren araştırma adımı olabileceği için sona alınmıştır; aksamaya uğrarsa MVP'nin etkilenmemesi için bu sıralama bilinçlidir.

---

## 18. Riskler ve Hafifletme Stratejileri

| Risk | Olasılık | Hafifletme |
|---|---|---|
| **NeoPixel BSS138 ile çalışmaz** | Yüksek | Doğrudan 3.3V sürme dene → Schottky diyot → son çare 74AHCT125 |
| **USB güç bütçesi yetmez** | Orta | Parlaklık %25-30, hoparlör seviyesi %80, gerekirse harici 5V/2A adaptör |
| **Wake word eğitimi yetersiz** | Orta | Buton zaten MVP'de çalışır; wake word başarısızsa "Future Work" sunulur |
| **Mikrofon-hoparlör akustik geri besleme** | Orta | SPEAKING durumunda mikrofon yazılımsal mute; fiziksel uzaklık |
| **Llama yanıt süresi >5 sn** | Düşük | Prompt cevapları 2-5 cümleyle sınırlar; streaming TTS ile ilk kelime hızlı |
| **USB bağlantı kopukluğu demo sırasında** | Düşük | Heartbeat ile algıla; ERROR durumu kırmızı flaş; yedek kablolar sınıfta |
| **ESP-IDF öğrenme eğrisi zaman alır** | Orta | Aşama 0 hazırlık dönemi (1 hafta); Arduino-as-component fallback |
| **MAX30102 ESP-IDF kütüphanesi yok** | Yüksek | Arduino-as-component ile SparkFun MAX3010x kullan |
| **I²C pull-up çakışması** | Düşük | MAX30102 modülündeki pull-up ölç; gerekirse lehimden kaldır |
| **MAX98357A click sesi** | Orta | SD pinini GPIO 10'a bağla, mute kontrolü uygula |
| **Hoparlörden Class-D EMI mikrofona girer** | Orta | Yıldız topraklama, kablolar kısa ve ayrı, twisted pair |
| **Hocanın yorum farklılığı (offline tanımı)** | Düşük | Örnek rapordaki "or local PC" alıntılanır; PSRAM eksikliği gerekçelendirilir |
| **Demo gününde donanım sorunu** | Orta | Önceden saatlerce test; yedek bileşenler; Wokwi simülasyonu backup |
| **Ses kalitesi mikrofonda kötü** | Düşük | 20-30 cm mesafeden konuşma; Whisper zaten gürültü dirençlidir |

---

## 19. Açık Konular ve Sonraki Adımlar

**Sistem promptu yazımı:** LLM'in rolünü, çıktı formatını ve sınırlarını belirleyen sistem promptu Aşama 9'da yazılacaktır.

**Demo senaryolarının seçimi:** Sınıfta hangi acil durum senaryolarının canlı gösterileceği henüz netleştirilmedi. Tipik adaylar: tıkanma (choking), kalp krizi şüphesi, ağır kanama, bilinç kaybı, alerjik şok. MVP saf LLM olduğu için aslında **her senaryo desteklenir**; demoda 3-5 tanesi seçilip pratik edilecek.

**Toplam gecikme bütçesi:** Kullanıcı butonu bıraktıktan sonra cevabın gelmesine kadar geçen toplam sürenin hedefi 3-5 saniye altıdır. Aşama 11'de ölçülecek.

**Mekanik kurulum:** Sistem prototip aşamasında breadboard üzerinde kalacak. İsteğe bağlı: 3D baskılı kutu veya akrilik panel.

**Zaman çizelgesi:** 1 Haziran 2026 deadline'ı için yaklaşık 3 hafta kalmış durumda (12 Mayıs itibarıyla). Geliştirme akışı sırasında haftalık adapte edilecek.

---

## 20. Sonuç

Bu plan, donanım raporu ve tasarım toplantılarının sonucunda netleşen mimari kararların tam dökümünü içerir. Üç temel ilke: **edge'de hafiflik, beyinde güç** (ESP32 hafif, PC ağır); **MVP-önce, iyileştirme-sonra** (saf LLM + buton ile başla, wake word ve hibrit sonra); ve **dürüst pragmatizm** (donanım kısıtlarını gizleme, açıkça gerekçelendir).

Geliştirme süreci 16 aşamalı yol haritasıyla yapılandırılmıştır. Aşama 11'de MVP demo edilebilir; sonraki aşamalar cilalama niteliğindedir. Wake word son aşamaya alınmıştır; gerekirse "Future Work" olarak sunulabilir.

Donanım kısıtları (PSRAM eksikliği, BSS138 level shifter sınırı, USB güç bütçesi) ve yazılım kısıtları (ESP-IDF öğrenme eğrisi, MAX30102 native ESP-IDF kütüphanesi yokluğu) açıkça tanınmış ve her biri için somut hafifletme stratejisi belirlenmiştir.

Bu raporun herhangi bir bölümünde değişiklik veya tartışılacak nokta varsa, o bölüm açılır ve tekrar konuşulur. Onayın sonrasında geliştirme aşamasına geçilir.

---

*Doküman v2 hazırlık tarihi: 12 Mayıs 2026 — Final teslim tarihi: 1 Haziran 2026*
*Önceki sürümden farklar: ESP-IDF geliştirme platformu netleşti, pin tablosu donanım raporu önerilerine göre düzeltildi, BSS138 level shifter sorunu eklendi, güç bütçesi detaylandırıldı, VAD kaldırıldı, wake word son aşamaya taşındı, etik bölümü dengelendi, riskler güncellendi.*
