# Kurulum ve Kullanım Rehberi (Sıfırdan)

Bu rehber, teknik bilgisi olmayan birinin de cihazı kurup çalıştırabilmesi için
adım adım yazılmıştır. Adımları sırayla uygula.

---

## Bölüm 0 — Gerekenler

### Donanım
| # | Parça | Not |
|---|---|---|
| 1 | **LilyGO T-CAN485** | https://lilygo.cc/en-us/products/t-can485 |
| 2 | **microSD kart** | 32 GB, FAT32 |
| 3 | **5V regülatör** | LM2596 / MP1584 (12V → 5V) |
| 4 | **OBD-II soket + kablo** | 4 tel yeterli |
| 5 | **USB kablosu** | Kartı bilgisayara yüklemek için |

### Yazılım (bilgisayara kurulacak)
| Yazılım | Link | Ne için |
|---|---|---|
| **VS Code** | https://code.visualstudio.com | editör |
| **PlatformIO** (VS Code eklentisi) | https://platformio.org/install/cli | derleme/yükleme |
| **Python 3.10+** | https://python.org/downloads | log analizi |

> **Not:** PlatformIO'yu komut satırından da kurabilirsin (daha hafif):
> `pip install platformio`. Bu rehber VS Code eklentisi üzerinden anlatılıyor,
> ama `pio ...` komutları da aynıdır.

---

## Bölüm 1 — Yazılımı kur

### 1.1 VS Code + PlatformIO
1. VS Code'u kur ve aç.
2. Sol taraftaki **Extensions** (eklentiler) simgesine tıkla (küp ikon).
3. Ara: **PlatformIO IDE** → **Install**.
4. Kurulum bittikten sonra VS Code'u bir kez yeniden başlat.

### 1.2 Python + analiz kütüphaneleri
1. Python'u kur (kurulumda **"Add Python to PATH"** kutusunu **işaretle**).
2. Bir terminal aç (VS Code'da `` Ctrl+` `` veya Windows "Komut İstemi").
3. Yaz:
   ```bash
   pip install -r requirements.txt
   ```
   (Bu komut `requirements.txt` dosyasındaki pandas, numpy, matplotlib'i kurar.)
   > `requirements.txt` repoda `tools/` altındadır; `cd tools` yapıp çalıştırmak en kolayı.

---

## Bölüm 2 — Kodu indir

```bash
git clone https://github.com/lepotane/T-CAN485-CANLogger.git
cd T-CAN485-CANLogger
```

Git yoksa: GitHub sayfasındaki **Code → Download ZIP** ile indir, çıkar.

---

## Bölüm 3 — SD kartı hazırla

1. microSD kartı bilgisayara tak.
2. Biçimlendir → dosya sistemi **FAT32** (Varsayılan 32GB için uygundur).
3. Kartı **T-CAN485'teki SD yuvasına** tak.
4. İlk açılışta cihaz kartın içine `sid00001/` gibi bir klasör açar ve oraya yazar.

---

## Bölüm 4 — Donanımı bağla

Kablo bağlantıları **[WIRING.md](WIRING.md)** dosyasında detaylı. Özet:

```
Aracın OBD-II soketi          T-CAN485
   Pin 16  +12V  ──► 5V regülatör ──► kartın 5V girişi
   Pin 4   GND   ─────────────────► GND
   Pin 6   CAN-H ─────────────────► CAN-H
   Pin 14  CAN-L ─────────────────► CAN-L
```

**Kritik 3 uyarı:**
1. Aracın **12V'unu doğrudan karta verme** — önce 5V regülatörden geçir.
2. Regülatör çıkışına **1000uF kondansatör** ekle (marşta voltaj düşer, kart resetlenmesin).
3. CAN-H / CAN-L ters olursa veri gelmez; hatalı bağlamak kartı bozmaz.

---

## Bölüm 5 — Firmware'i derle ve yükle

1. Kartı **USB kablosuyla** bilgisayara tak.
2. VS Code'ta bu projeyi aç (`File → Open Folder → T-CAN485-CANLogger`).
3. Alt taraftaki **PlatformIO** çubuğundan (veya sol taraftaki ✓ ikonu):
   - **Build** (derle) → hata görmüyorsan devam.
   - **Upload** (yükle) → kart flash'lanır.

Komut satırı alternatifi:
```bash
pio run -e esp32dev              # derle
pio run -e esp32dev -t upload    # yükle
```

> İlk derleme ESP32 araç setini indirir, birkaç dakika sürebilir (bir kez).

---

## Bölüm 6 — Test et (masaüstü)

Kart hâlâ USB'deyken seri konsolu aç:
```bash
pio device monitor
```
(115200 baud. VS Code'ta PlatformIO "Monitor" düğmesi de aynı işi yapar.)

Konsolda şunları görmen gerekir:
```
T-CAN485-CANLogger v1.0.0
Ultra-detailed CAN / OBD-II logger
[SD] session 1 mounted, dir /sid00001
[CAN] up at 500 kbit/s (TX=27 RX=26)
[OBD] poller started
Type 'help' for commands.
```

Şimdi `status` yaz + Enter:
```
[STAT] up=10s eng=off can=1234 drop=0 raw=0MB csv=0MB sid=1
```
- `can=1234` sayısı **artıyorsa** CAN veriyolu okunuyor ✓
- `can=0` kalıyorsa CAN bağlantısını kontrol et (WIRING.md).

**Yararlı komutlar:** `help`, `status`, `pids` (canlı değerler), `dtc`, `flush`, `reboot`.

> Masaüstünde araç yoksa `can=0` normaldir. Gerçek test için araca tak.

---

## Bölüm 7 — Araca tak ve kaydet

1. `flush` yaz (SD'yi temizle) veya kartı tak-çıkar.
2. Cihazı aracın **OBD-II soketine** tak (güç regülatörü bağlanmış halde).
3. Kontağı **kapat**, aracı olduğu gibi bırak.
4. Cihaz uyanık kalır ve **her şeyi** kaydeder. Durum LED'i **yeşil** (motor duruyor)
   veya **cyan** (motor çalışıyor).
5. **1-2 gün** bekle. Gün içinde aracı normal kullan / çalıştır (cold start'lar olsun).

---

## Bölüm 8 — Kartı al ve analiz et

1. Cihazı çıkar, microSD kartı bilgisayara tak.
2. Kartın içindeki `sid00001` (veya `sid00002`...) klasörünü bul.
3. Analizi çalıştır:
   ```bash
   cd T-CAN485-CANLogger\tools
   python analyze_logs.py E:\sid00001 --out analiz
   ```
   (`E:\sid00001` yerine kendi klasör yolunu yaz.)
4. `analiz` klasörüne şunlar çıkar:
   - `sid00001_start0_COLD.png` → cold start anının grafikleri
   - `sid00001_merged.csv` → tüm veriler tek tabloda
   - `sid00001_raw_ids.txt` → hangi CAN ID'leri yoğun (özel sinyalleri bulmak için)
   - Konsolda **DTC listesi** ve **tekleme metriği**

Grafikleri aç, özellikle `rpm` (tekleme = dalgalanma), `stft_b1_pct` (yakıt sapması),
`o2_b1s1_equiv` (lambda) ve `ect_c` (soğutma sıvısı) çizgilerine bak.

---

## Bölüm 9 — Ayarları değiştirme (opsiyonel)

Tüm ayarlar **`src/config.h`** dosyasında. Değiştirdikten sonra yeniden derle.

| Ayar | Ne yapar | Varsayılan |
|---|---|---|
| `RAW_MODE` | 0=ham yok, 1=başlangıç penceresi, 2=hep | `2` |
| `CSV_ROW_INTERVAL_MS` | CSV satır hızı (50 = 20 Hz) | `50` |
| `FILE_ROTATE_BYTES` | Dosya boyutu limiti | 512 MB |
| `DIAG_SNAPSHOT_SEC` | DTC/Mode06 sıklığı | 60 sn |
| `DEEP_SLEEP_ENABLED` | Uzun parkta uyku (pil tasarrufu) | `0` |
| `PID_TABLE` | Hangi PID'ler, hangi hızda | (tablodaki) |

Yeni PID eklemek için `PID_TABLE`'a satır ekle (SAE J1979 PID + decode fonksiyonu).

---

## Sık yapılan hatalar

**"SD mount failed"**
- Kart FAT32 mü? Düzgün oturdu mu? Başka kart dene. 32GB altı/üstü olabilir.

**"CAN init failed" / `can=0`**
- CAN-H / CAN-L ters mi? Bağlantı gevşek mi?
- Kart araca bağlıyken kontak açık mı? (Bazı araçlarda OBD veriyolu kontak ister.)

**Derleme hatası (PIO bulunamadı)**
- PlatformIO kuruldu mu? VS Code'u yeniden başlat.
- `pio --version` çalışmalı.

**Python hatası (module not found)**
- `pip install -r requirements.txt` çalıştı mı? `cd tools` yapmayı unutma.

Daha fazlası için **[TROUBLESHOOTING.md](TROUBLESHOOTING.md)**.

---

## Güvenlik

- Cihaz yalnızca **okur/dinler**, ECU'ya komut **yazmaz**.
- Araca kalıcı bağlamadan önce hatta **1-2A sigorta** koy.
- Aracı çalışırken kablo bağlantısıyla oynarken dikkatli ol.