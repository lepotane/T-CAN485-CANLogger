#include "sdlog.h"
#include "pin_config.h"
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>

// ----------------------------------------------------------------------------
// Session / file state
// ----------------------------------------------------------------------------
static Preferences s_prefs;
static uint32_t s_sid  = 0;
static File s_rawFile, s_csvFile, s_diagFile;
static char s_dir[32];
static int  s_rawIdx = 0, s_csvIdx = 0, s_diagIdx = 0;

static uint64_t s_rawBytes = 0, s_csvBytes = 0;
static bool s_cardOk = false;
static bool s_csvHeaderWritten = false;
static uint64_t s_epochBase = 0;

// RAW_MODE 1 windowing
static bool     s_engineRunning = false;
static uint32_t s_windowUntilMs = 0;

// write buffers
static uint8_t s_rawBuf[32 * 1024];
static size_t  s_rawLen = 0;
static char    s_csvBuf[16 * 1024];
static size_t  s_csvLen = 0;

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------
static int find_next_index(const char *dir, const char *prefix)
{
    int maxIdx = -1;
    File d = SD.open(dir);
    if (!d) return 0;
    File e;
    size_t plen = strlen(prefix);
    while ((e = d.openNextFile())) {
        const char *name = e.name();
        if (strncmp(name, prefix, plen) == 0) {
            int idx = atoi(name + plen);
            if (idx > maxIdx) maxIdx = idx;
        }
        e.close();
    }
    d.close();
    return maxIdx + 1;
}

static void write_raw_header(File &f, uint32_t sid)
{
    uint8_t hdr[16];
    memset(hdr, 0, 16);
    memcpy(hdr, RAW_MAGIC, 8);
    hdr[8]  = (uint8_t)(RAW_FORMAT_VER & 0xFF);
    hdr[9]  = (uint8_t)(RAW_FORMAT_VER >> 8);
    hdr[10] = 0; hdr[11] = 0;                 // flags
    hdr[12] = (uint8_t)(sid & 0xFF);
    hdr[13] = (uint8_t)((sid >> 8) & 0xFF);
    hdr[14] = (uint8_t)((sid >> 16) & 0xFF);
    hdr[15] = (uint8_t)((sid >> 24) & 0xFF);
    f.write(hdr, 16);
}

static void open_raw()
{
    char path[64];
    snprintf(path, sizeof(path), "%s/raw_%03d.bin", s_dir, s_rawIdx);
    s_rawFile = SD.open(path, FILE_WRITE);
    if (s_rawFile) {
        write_raw_header(s_rawFile, s_sid);
        s_rawBytes = 16;
        Serial.printf("[SD] raw -> %s\n", path);
    }
}

static void open_csv()
{
    char path[64];
    snprintf(path, sizeof(path), "%s/pid_%03d.csv", s_dir, s_csvIdx);
    s_csvFile = SD.open(path, FILE_WRITE);
    s_csvHeaderWritten = false;
    if (s_csvFile) {
        s_csvBytes = 0;
        Serial.printf("[SD] csv -> %s\n", path);
    }
}

static void open_diag()
{
    char path[64];
    snprintf(path, sizeof(path), "%s/diag_%03d.csv", s_dir, s_diagIdx);
    s_diagFile = SD.open(path, FILE_WRITE);
    if (s_diagFile) {
        Serial.printf("[SD] diag -> %s\n", path);
    }
}

// ----------------------------------------------------------------------------
// Begin
// ----------------------------------------------------------------------------
bool sdlog_begin()
{
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI, 20000000)) {
        Serial.println("[SD] mount FAILED");
        return false;
    }
    s_cardOk = true;

    s_prefs.begin("canlog", false);
    bool fromDeepSleep = (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED);
    if (!fromDeepSleep) {
        s_sid = s_prefs.getUInt("sid", 0) + 1;
        s_prefs.putUInt("sid", s_sid);
    } else {
        s_sid = s_prefs.getUInt("sid", 1);
    }

    snprintf(s_dir, sizeof(s_dir), "/sid%05u", s_sid);
    SD.mkdir(s_dir);

    s_rawIdx  = find_next_index(s_dir, "raw_");
    s_csvIdx  = find_next_index(s_dir, "pid_");
    s_diagIdx = find_next_index(s_dir, "diag_");

    open_raw();
    open_csv();
    open_diag();

    // metadata
    char infoPath[64];
    snprintf(infoPath, sizeof(infoPath), "%s/info.txt", s_dir);
    File info = SD.open(infoPath, FILE_APPEND);
    if (info) {
        info.printf("session=%u part_raw=%d part_csv=%d\n", s_sid, s_rawIdx, s_csvIdx);
        info.printf("fw=%s %s\n", FW_NAME, FW_VERSION);
        info.printf("raw_mode=%d rotate=%llu row_ms=%d\n", RAW_MODE,
                    FILE_ROTATE_BYTES, CSV_ROW_INTERVAL_MS);
        info.printf("boot_reason=%d boot_count=%u\n",
                    (int)esp_sleep_get_wakeup_cause(), s_prefs.getUInt("boot", 0) + 1);
        info.close();
    }
    s_prefs.putUInt("boot", s_prefs.getUInt("boot", 0) + 1);

    Serial.printf("[SD] session %u mounted, dir %s\n", s_sid, s_dir);
    return true;
}

void sdlog_set_epoch(uint64_t baseUnixMs) { s_epochBase = baseUnixMs; }

void sdlog_set_engine_state(bool running)
{
    if (running && !s_engineRunning) {
        s_windowUntilMs = millis() + (uint32_t)RAW_WINDOW_SEC * 1000UL;
    }
    s_engineRunning = running;
}

// ----------------------------------------------------------------------------
// Writers
// ----------------------------------------------------------------------------
void sdlog_write_raw(const RawFrame &f)
{
    if (RAW_MODE == 0) return;
    if (RAW_MODE == 1 && !s_engineRunning && millis() > s_windowUntilMs) return;

    uint8_t *p = &s_rawBuf[s_rawLen];
    uint32_t ts = f.tsMs;
    memcpy(p, &ts, 4);
    memcpy(p + 4, &f.id, 4);
    p[8] = f.dlc;
    p[9] = 0;
    memcpy(p + 10, f.data, 8);
    s_rawLen += RAW_RECORD_LEN;
    s_rawBytes += RAW_RECORD_LEN;

    if (s_rawLen + RAW_RECORD_LEN > sizeof(s_rawBuf)) {
        sdlog_flush();
    }
}

void sdlog_write_csv_row(const float *vals, const bool *present)
{
    char line[2048];
    size_t off = 0;

    if (!s_csvHeaderWritten) {
        off += snprintf(line + off, sizeof(line) - off, "unix_ms,ms");
        for (size_t i = 0; i < PID_COUNT; i++) {
            off += snprintf(line + off, sizeof(line) - off, ",%s", PID_TABLE[i].label);
        }
        off += snprintf(line + off, sizeof(line) - off, "\n");
        s_csvHeaderWritten = true;
    }

    uint64_t unixMs = s_epochBase ? (s_epochBase + millis()) : 0;
    off += snprintf(line + off, sizeof(line) - off, "%llu,%lu",
                    (unsigned long long)unixMs, (unsigned long)millis());

    for (size_t i = 0; i < PID_COUNT; i++) {
        if (present[i]) {
            off += snprintf(line + off, sizeof(line) - off, ",%.3f", vals[i]);
        } else {
            off += snprintf(line + off, sizeof(line) - off, ",");
        }
    }
    off += snprintf(line + off, sizeof(line) - off, "\n");

    if (off > sizeof(line)) off = sizeof(line);
    if (s_csvLen + off > sizeof(s_csvBuf)) {
        sdlog_flush();
    }
    memcpy(&s_csvBuf[s_csvLen], line, off);
    s_csvLen += off;
    s_csvBytes += off;
}

void sdlog_write_diag(const char *line)
{
    if (!s_diagFile) return;
    s_diagFile.println(line);
    s_diagFile.flush();
}

// ----------------------------------------------------------------------------
// Flush / rotate / service
// ----------------------------------------------------------------------------
void sdlog_flush()
{
    if (s_rawLen && s_rawFile) {
        s_rawFile.write(s_rawBuf, s_rawLen);
        s_rawFile.flush();
        s_rawLen = 0;
    }
    if (s_csvLen && s_csvFile) {
        s_csvFile.write((uint8_t *)s_csvBuf, s_csvLen);
        s_csvFile.flush();
        s_csvLen = 0;
    }
}

void sdlog_service()
{
    // rotation
    if (s_rawFile && s_rawBytes >= FILE_ROTATE_BYTES) {
        s_rawFile.close();
        s_rawIdx++;
        open_raw();
    }
    if (s_csvFile && s_csvBytes >= FILE_ROTATE_BYTES) {
        s_csvFile.close();
        s_csvIdx++;
        open_csv();
    }
    sdlog_flush();
}

uint32_t sdlog_session_id() { return s_sid; }
uint32_t sdlog_part()       { return s_rawIdx; }
uint64_t sdlog_raw_bytes()  { return s_rawBytes; }
uint64_t sdlog_csv_bytes()  { return s_csvBytes; }
bool     sdlog_card_ok()    { return s_cardOk; }