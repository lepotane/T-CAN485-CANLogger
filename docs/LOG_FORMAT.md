# Log Dosya Formatları (Teknik)

Cihaz, SD kartın kökünde oturum başına bir klasör açar:

```
sid00001/
  info.txt          # oturum bilgisi
  pid_000.csv       # decode edilmiş PID'ler (geniş format)
  pid_001.csv       # (rotasyon — yeni dosya)
  raw_000.bin       # her ham CAN frame (ikili)
  raw_001.bin       # (rotasyon)
  diag_000.csv      # DTC + Mode 06 anlık görüntüleri
```

Oturum numarası (`sid`) her **açılışta** artar (derin uyku uyanışları aynı oturumu
sürdürür, dosya numarası artar). Dosyalar `FILE_ROTATE_BYTES` (varsayılan 512 MB)
boyutuna ulaşınca yenisi açılır.

---

## 1. `raw_*.bin` — Ham CAN frame'leri

Bayt bayt (little-endian). Önce **16 baytlık dosya başlığı**, ardından **18 baytlık**
sabit kayıtlar.

### Dosya başlığı (16 bayt)
| Ofset | Tip | İçerik |
|---|---|---|
| 0 | char[8] | Sihirli kelime `"TC485RAW"` |
| 8 | uint16 | Format sürümü (`0x0001`) |
| 10 | uint16 | Bayraklar (şimdilik 0) |
| 12 | uint32 | Oturum kimliği (`sid`) |

### Kayıt (18 bayt)
| Ofset | Tip | İçerik |
|---|---|---|
| 0 | uint32 | `ts_ms` — oturum başından itibaren milisaniye |
| 4 | uint32 | `can_id` — CAN tanımlayıcı. Bit31 = geniş (29-bit), Bit30 = RTR, kalan = ID |
| 8 | uint8 | `dlc` — veri uzunluğu (0-8) |
| 9 | uint8 | Ayrılmış (0) |
| 10 | uint8[8] | `data` — 8 veri baytı (kullanılmayanlar 0) |

Kayıt başına dosyada 18 bayt. Toplam kayıt sayısı = `(dosya_boyutu - 16) / 18`.

### Python'da okuma örneği
```python
import struct
data = open("raw_000.bin", "rb").read()
off = 16
while off + 18 <= len(data):
    ts, can_id = struct.unpack("<II", data[off:off+8])
    dlc = data[off+8]
    ext = (can_id >> 31) & 1
    cid = can_id & 0x1FFFFFFF
    payload = data[off+10:off+18]
    print(ts, hex(cid), dlc, payload.hex())
    off += 18
```

---

## 2. `pid_*.csv` — Decode edilmiş OBD-II değerleri

Geniş format: **ilk satır başlık**, ardından her satır bir zaman örneklemesi
(`CSV_ROW_INTERVAL_MS` aralıkla, varsayılan 50 ms ≈ 20 Hz).

İlk iki sütun zaman, sonraki sütunlar `src/config.h` içindeki `PID_TABLE` sırasıyla.

```
unix_ms,ms,rpm,speed_kmh,throttle_pct,pedal_d_pct,...,ect_c,...
0,50,0.000,0.000,...,20.000,...
0,100,0.000,0.000,...,20.000,...
```

| Sütun | Birim | Açıklama |
|---|---|---|
| `unix_ms` | ms | Duvar saati (ayarlanmadıysa 0) |
| `ms` | ms | Oturum başından itibaren milisaniye |
| `rpm` | dev/dk | Motor devri |
| `speed_kmh` | km/s | Araç hızı |
| `throttle_pct` | % | Gaz kelebeği konumu |
| `pedal_d_pct` / `pedal_e_pct` | % | **Gaz pedalı basılma yüzdesi** |
| `maf_gps` | g/s | Hava debisi |
| `map_kpa` | kPa | Emme manifold basıncı (boost göstergesi) |
| `engine_load_pct` / `abs_load_pct` | % | Motor yükü |
| `stft_b1_pct` / `ltft_b1_pct` | % | Kısa/uzun dönem yakıt düzeltmesi |
| `o2_b1s1_equiv` | lambda | Hava-yakıt oranı (1.0 = stokiyometrik) |
| `timing_adv_deg` | derece | Ateşleme ilerleme açısı |
| `fuel_press_kpa` / `rail_abs_kpa` | kPa | Yakıt basıncı |
| `ect_c` / `iat_c` / `oil_temp_c` | °C | Sıcaklıklar |
| `cat_b1s1_c` | °C | Katalizör sıcaklığı |
| `module_v` | V | Kontrol modülü voltajı |
| `drv_torque_pct` / `act_torque_pct` | % | Tork |

Tam liste ve formüller: `src/config.h` → `PID_TABLE`.

Boş hücre = o an o PID okunamamış (motor kapalıyken sıcaklık/voltaj vb. olabilir).

---

## 3. `diag_*.csv` — Teşhis anlık görüntüleri

Serbest metin, satır başına bir olay. Format: `t=<ms>,<tip>,...`

| Tip | Örnek | Anlamı |
|---|---|---|
| `stored`/`pending`/`permanent` + `DTC` | `t=5000,stored,DTC,P0301` | Arıza kodu |
| `stored` + `raw=` | `t=5000,stored,raw=43010300` | Ham cevap (hex) |
| `mode06` | `t=5000,mode06,00,raw=4600...` | Mode 06 monitör taraması |

DTC formatı: `P0301` = P (güç aktarma), 0, 3, 0, 1 → silindir 1 ateşleme tekleme.

---

## 4. `info.txt` — Oturum bilgisi

```
session=1 part_raw=0 part_csv=0
fw=T-CAN485-CANLogger 1.0.0
raw_mode=2 rotate=536870912 row_ms=50
boot_reason=0 boot_count=1
```

---

## Notlar

- `ts_ms` / `ms` oturum başlangıcına göredir; derin uyku uyanışında sıfırlanır.
  `analyze_logs.py` bunu otomatik algılar ve veriyi segmentlere böler.
- Duvar saatini istiyorsan seri konsoldan `time <epoch_sniye>` yaz; `unix_ms`
  sütunu dolar.
- Ham frame'ler tüm veriyolunu içerir (aracın diğer ECU'ları dahil). Özel
  Mercedes sinyallerini bulmak için `analyze_logs.py`'nin ürettiği
  `*_raw_ids.txt` raporuna bak.