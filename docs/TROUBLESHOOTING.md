# Sorun Giderme (TROUBLESHOOTING)

## Kurulum / derleme

### `pio: command not found` / PlatformIO bulunamadı
- PlatformIO kuruldu mu? https://platformio.org/install/cli
- VS Code eklentisini kurduysan VS Code'u **yeniden başlat**.
- Komut satırı kullanıyorsan: `pip install platformio`.
- Test: `pio --version` bir sürüm yazmalı.

### İlk derleme çok uzun sürüyor
- Normal. ESP32 araç setini bir kez indirir (~200 MB). Sonraki derlemeler hızlı.

### `lib/Mylibrary/pin_config.h: No such file`
- Projeyi tam klasör olarak aç (`File → Open Folder → T-CAN485-CANLogger`),
  tek tek dosya olarak değil.
- `lib/Mylibrary/pin_config.h` dosyası repoda durmalı.

### Upload (yükleme) başarısız
- USB kablosu veri taşıyor mu? (Sadece şarj kablosu olabilir.)
- Doğru port seçili mi? (PlatformIO port listesinden seç.)
- Başka bir program portu tutuyor olabilir (`pio device monitor` açık olmasın).
- Boot tuşuna basarak yüklemeyi dene (bazı kartlarda).

---

## SD kart

### `SD mount failed` / `SD card initialization failed`
- Kart **FAT32** formatlı mı? (NTCS/exFAT olmaz — FAT32 yap.)
- Kart doğru oturdu mu? Çıkar-tak.
- Kart çok büyükse (>64GB) formatlayarak dene. 32GB ideal.
- Başka kart dene (bazı kartlar SPI modunda sorun çıkarır).
- SD pinleri sabit: `SCLK=14, MISO=2, MOSI=15, CS=13` (`pin_config.h`).

### Kart dolu / dosya yok
- `FILE_ROTATE_BYTES` 512MB'de dosya döndürür, eski dosyalar silinmez; kart dolar.
  2 günlük ~6GB beklenir. Kart yetersizse `RAW_MODE` değerini düşür (`config.h`).

---

## CAN / veri

### `can=0` (hiç frame gelmiyor)
1. **CAN-H / CAN-L ters mi?** → yer değiştir, dene (kart bozulmaz).
2. **Kablo bağlantısı gevşek mi?** → OBD soketi pin 6 (CAN-H), pin 14 (CAN-L).
3. **Kontak açık mı?** Bazı araçlarda OBD veriyolu kontak ister (motor çalışmıyorken).
4. **Güç var mı?** `ME2107_EN` (GPIO16) HIGH yapılmalı — firmware otomatik yapıyor,
   ama kart beslenmiyorsa (5V regülatör) transceiver çalışmaz.
5. Masaüstünde (araç yokken) `can=0` **normaldir**.

### CAN sayıları artıyor ama PID değerleri boş
- ECU cevap vermiyor olabilir. `dtc` komutunu dene.
- `OBD_REQ_ID` (0x7E0) bu araç için doğru olmalı. Farklı ECU adresi gerekebilir.

### `eng=off` ama araç çalışıyor
- RPM PID'si (0x0C) okunamıyor. CAN bağlantısını ve `pids` çıktısını kontrol et.

---

## Güç / çalışma

### Kart sürekli resetleniyor (marşta)
- Marşta voltaj ~9V'a düşer. 5V regülatör çıkışına **1000uF** kondansatör ekle.
- Regülatör gücü yetersiz olabilir; daha güçlü regülatör dene.

### LED yanmıyor
- `config.h` → `LED_ENABLED 1` mi? WS2812B GPIO4'te.
- LED kozmetik; loglama LED'siz de çalışır.

### Akü boşalıyor (uzun park)
- Varsayılan `DEEP_SLEEP_ENABLED 0` (cihaz hep uyanık). Uzun park için `1` yap
  (`config.h`) — ama ilk marşları kaçırabilirsin.
- 1-2 günlük kullanımda sağlıklı aküde sorun olmaz (~60-100 mA).

---

## Analiz

### `module not found: pandas` / `matplotlib`
```bash
cd tools
pip install -r requirements.txt
```
- Python PATH'e ekli mi? Kurulumda "Add to PATH" işaretli miydi?

### `path not found` / boş analiz
- Yol doğru mu? Örnek: `python analyze_logs.py E:\sid00001 --out analiz`
- Klasör `sid...` mi ve içinde `pid_*.csv` var mı?

### Grafik yok / soğuk başlangıç bulunamadı
- `--cold-ect` değerini ayarla (varsayılan 40°C). Soğuk motor eşiği olabilir.
- Veride gerçekten start (motor dururken→çalışırken) geçişi var mı?

---

## Seri konsol

### Konsol boş / garip karakterler
- Baud **115200** olmalı. `pio device monitor` doğru seçer.
- Doğru portta mı? Başka terminal açık olmasın.

### Komut çalışmıyor
- `help` yaz, komut listesini gör. Satır sonunda **Enter** gerekiyor.

---

## Hâlâ olmadıysa

1. `pio run -e esp32dev` temiz derleme yapıyor mu? (Hata metnini not al.)
2. `pio device monitor` çıktısını not al (özellikle hata satırları).
3. `analyze_logs.py` çıktısını not al.
4. `docs/INSTALL.md` Bölüm 6'daki beklenen konsol çıktısıyla karşılaştır.