# T-CAN485 Ultra-Detailed CAN / OBD-II Logger

LilyGO **T-CAN485** (ESP32 + CAN + RS485) ile çalışan, araca takılıp **1-2 gün kalan**
ve **SD karta her şeyi kaydeden** veri logger'ı.

> **Bu ne işe yarar?** Aracın çalıştırma anındaki (cold start) davranışını —
> hava, yakıt, basınç, ateşleme, lambda, tekleme — saniyede ~20 kez kaydeder.
> Sonra bilgisayarda grafik + analiz ile sorunun kaynağını bulursun.

Hedef araç: **Mercedes W204 C180K 1.6 ML Kompressor (ECU: Siemens/Continental SIM271KE)**,
egzoz turbosu eklenerek **twincharger** yapılmış. Amaç: **cold start tekleme**
sorununu, ECU harita düzeltmesi için yeterli detayda kaydetmek.

---

## 📦 İçindekiler

| Dosya | Ne var |
|---|---|
| **[docs/INSTALL.md](docs/INSTALL.md)** | ⭐ Sıfırdan kurulum + kullanım (bir kişi rehberi) |
| [docs/WIRING.md](docs/WIRING.md) | Araca nasıl bağlanır (kablo, pinout) |
| [docs/LOG_FORMAT.md](docs/LOG_FORMAT.md) | Kaydedilen dosya formatları (teknik) |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Sık hatalar ve çözümleri |
| [src/config.h](src/config.h) | PID tablosu, kaydetme oranları (ayarlar) |
| [tools/analyze_logs.py](tools/analyze_logs.py) | Log analiz + grafik aracı |

---

## ✨ Özellikler

- **Her ham CAN frame'ini** kaydeder (`raw_*.bin`) — Mercedes'e özel sinyaller dahil.
- **Decode edilmiş OBD-II verileri** (`pid_*.csv`, ~20 Hz): gaz pedalı %, throttle,
  hava debisi (MAF), boost/MAP, yakıt basıncı, ateşleme açısı, kısa/uzun dönem yakıt
  düzeltmesi (STFT/LTFT), lambda, katalizör sıcaklığı, tork, voltaj vb.
- **Teşhis**: stored/pending/permanent arıza kodları (DTC) + Mode 06 misfire monitörü.
- **Otomatik**: SD kart rotasyonu, motor çalıştırma algılama, durum LED'i.
- **Offline analiz**: cold-start olaylarını bulur, tekleme (roughness) metriği çıkarır,
  grafik çizer.

---

## 🛒 Gerekenler (donanım)

1. **LilyGO T-CAN485** kartı — https://lilygo.cc/en-us/products/t-can485
2. **microSD kart** — FAT32 formatlı, **32 GB** önerilir.
3. **5V regülatör** — LM2596 veya MP1584 (aracın 12V'unu 5V'a düşürür).
4. **OBD-II kablo** — 4 kablo yeterli (+12V, GND, CAN-H, CAN-L).
5. **USB kablosu** (kartı bilgisayara yüklemek için) + bilgisayar.

## 💻 Gerekenler (yazılım)

| Yazılım | Ne için | Link |
|---|---|---|
| **PlatformIO** (VS Code eklentisi veya CLI) | Firmware derleme/yükleme | https://platformio.org/install/cli |
| **Python 3.10+** | Log analizi | https://python.org/downloads |
| Espressif32 platform (otomatik iner) | ESP32 desteği | PlatformIO kurarken otomatik |

> Arduino kütüphanesi **gerekmez**. Tüm CAN/SD kütüphaneleri ESP32 çekirdeğinde gömülüdür.

---

## 🚀 Hızlı başlangıç (özet)

Tam rehber için **[docs/INSTALL.md](docs/INSTALL.md)**. Özet:

```bash
# 1. Depoyu indir
git clone https://github.com/lepotane/T-CAN485-CANLogger.git
cd T-CAN485-CANLogger

# 2. Firmware'i derle ve karta yükle (USB takılı)
pio run -e esp32dev -t upload

# 3. Konsolu aç → "status" yaz, CAN sayacı artıyorsa OK
pio device monitor

# 4. SD kartı tak, cihazı araca bağla, 1-2 gün bekle

# 5. Kartı al, analiz et
cd tools
pip install -r requirements.txt
python analyze_logs.py E:\sid00001 --out analiz
```

---

## 📁 Proje yapısı

```
platformio.ini                  # derleme ayarları
lib/Mylibrary/pin_config.h      # T-CAN485 pin haritası
src/
  config.h                      # PID tablosu, oranlar, format (AYARLAR)
  canbus.h/.cpp                 # CAN (TWAI) init, ISO-TP, OBD istek/cevap
  obd2.h/.cpp                   # PID decode, poller, DTC, Mode 06
  sdlog.h/.cpp                  # SD oturum, rotasyon, binary+CSV yazıcı
  led.h/.cpp                    # WS2812B durum LED (bit-bang)
  main.cpp                      # orkestrasyon + seri komutlar
tools/
  analyze_logs.py               # offline analiz + grafik
  requirements.txt
docs/                           # tüm dokümantasyon
```

## ⚠️ Güvenlik

- Cihaz yalnızca **dinler** + standart OBD-II **okuma** istekleri gönderir.
- ECU'ya yazma / şifre çözme / tuning komutu **göndermez**.
- Aracın 12V hattına kalıcı bağlanırken **1-2A sigorta** kullan.

