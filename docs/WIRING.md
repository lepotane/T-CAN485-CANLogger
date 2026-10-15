# Bağlantı - T-CAN485 -> Mercedes W204 OBD-II

## 1. Güç (kritik)

| OBD-II pin | İşlev | T-CAN485 |
|---|---|---|
| **16** | +12V (aracın sabit aküsü) | 5V regülatör çıkışı -> kartın 5V/VIN girişi |
| **4** | Şasi GND | GND |
| 5 | Sinyal GND | (opsiyonel, GND ile birleştirilebilir) |

- Aracın 12V'u **5V'a düşürülerek** beslenmeli (kart 5V ile çalışır).
  Önerilen: **LM2596 / MP1584** step-down + giriş-çıkış kondansatörleri.
- Marşta voltaj ~9V'a düşer. Regülatör + kart bu düşüşe dayanıklı olmalı.
  Gerekirse regülatör girişine **1000uF** kondansatör ekle, resetlenmeyi önler.
- Doğrudan 12V'U KARTA VERME. Kart 5V lojik besleme ister.

## 2. CAN veriyolu (W204 CAN-C, 500 kbps, 11-bit)

| OBD-II pin | İşlev |
|---|---|
| **6** | CAN-High |
| **14** | CAN-Low |

| T-CAN485 soketi | Bağlanacak |
|---|---|
| **CAN-H** | OBD pin 6 |
| **CAN-L** | OBD pin 14 |

Notlar:
- Araca özel, sokette zaten **120 ohm** terminasyon var; ayrıca direnç ekleme.
- CAN-H / CAN-L ters bağlarsan veri gelmez (hasar vermez, düzeltip dene).
- Kalıcı montajda kabloyu kısa tut (< 1 m), çift bükümlü/ekranlı kullan.

## 3. T-CAN485 pinleri (firmware sabit)

| İşlev | GPIO |
|---|---|
| CAN_TX | 27 |
| CAN_RX | 26 |
| CAN_SPEED_MODE (HIGH=hızlı) | 23 -> firmware **LOW** (high-speed) yapar |
| ME2107_EN (transceiver güç) | 16 -> firmware **HIGH** yapar |
| SD SCLK / MISO / MOSI / CS | 14 / 2 / 15 / 13 |
| WS2812B durum LED | 4 |

> `ME2107_EN` HIGH yapılmazsa CAN/RS485 transceiver'ı beslenmez, hiçbir frame
> gelmez. Firmware bunu `canbus_begin()` içinde otomatik yapıyor.

## 4. SD kart
- **FAT32**, 32 GB önerilir.
- Takılı ve düzgün oturmuş olmalı; `SD mount failed` görürsen kartı yeniden tak.

## 5. LED anlamları

| Renk | Durum |
|---|---|
| Mavi | Açılış |
| Yeşil | Loglama (motor duruyor) |
| Cyan | Loglama + motor çalışıyor / raw penceresi |
| Kırmızı (yanıp söner) | SD hatası |
| Turuncu (yanıp söner) | ECU'dan cevap yok |
| Mor | Derin uyku |

## 6. Kurulum sırası
1. SD kartı tak.
2. Kartı OBD soketine + güç regülatörüne bağla.
3. Cihazı USB ile bilgisayara tak, `pio run -t upload` ile firmware yükle (bir kez).
4. `pio device monitor` aç → `status` yaz, CAN sayacı artıyorsa veri geliyor.
5. `time <epoch_s>` (opsiyonel) ile saati ayarla.
6. Araca tak, kontağı kapat, 1-2 gün bırak.
7. Kartı al, `tools/analyze_logs.py` ile analiz.

## 7. Güvenlik
- Cihaz yalnızca **dinler** + standart OBD-II okuma istekleri gönderir.
- ECU'ya yazma / kilid açma / tuning komutu **göndermez**.
- Aracın 12V sistemine kalıcı bağlantı yapmadan önce sigortayı (1-2A) unutma.