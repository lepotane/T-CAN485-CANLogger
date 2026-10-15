/*
 * T-CAN485 ultra-detailed CAN / OBD-II data logger
 * ------------------------------------------------
 * Mercedes W204 C180K 1.6 ML Kompressor (SIM271KE) -> twincharger.
 * Logs everything to SD: every raw CAN frame + decoded OBD-II PIDs + periodic
 * DTC / Mode-06 snapshots. Pull the card after 1-2 days and analyse with
 * tools/analyze_logs.py.
 */
#include <Arduino.h>
#include "config.h"
#include "canbus.h"
#include "obd2.h"
#include "sdlog.h"
#include "led.h"

// ---- state -----------------------------------------------------------------
static uint32_t s_lastRowMs = 0;
static uint32_t s_lastDiagMs = 0;
static uint32_t s_lastStatusMs = 0;
static uint32_t s_lastEngineOnMs = 0;
static bool     s_engineWasRunning = false;
static char     s_cliBuf[96];
static size_t   s_cliLen = 0;

// ---- forward decls ---------------------------------------------------------
static void run_diag_snapshot();
static void print_status();
static void print_pids();
static void handle_cli_line(char *line);

// ============================================================================
void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(200);
    Serial.printf("\n\n%s v%s\n", FW_NAME, FW_VERSION);
    Serial.println("Ultra-detailed CAN / OBD-II logger");

    led_begin();
    led_set(LED_BOOT);

    if (!sdlog_begin()) {
        Serial.println("!! SD mount failed - logging to serial only");
        led_set(LED_SD_ERROR);
    }

    if (!canbus_begin()) {
        Serial.println("!! CAN init failed - halting");
        led_set(LED_SD_ERROR);
        for (;;) { led_service(); delay(100); }
    }

    obd_begin();

    Serial.println("\nType 'help' for commands.\n");
}

// ============================================================================
void loop()
{
    uint32_t now = millis();

    // 1) Drain raw CAN frames into the SD raw log (batch per pass).
    RawFrame f;
    for (int i = 0; i < 512; i++) {
        if (!can_pop_raw(f, 0)) break;
        sdlog_write_raw(f);
    }

    // 2) Engine state (drives RAW_MODE 1 window + LED + sleep).
    bool running = obd_engine_running();
    if (running) s_lastEngineOnMs = now;
    if (running != s_engineWasRunning) {
        sdlog_set_engine_state(running);
        s_engineWasRunning = running;
        Serial.printf("[ENG] %s\n", running ? "RUNNING" : "OFF");
    }

    // 3) Decoded PID CSV row at CSV_ROW_INTERVAL_MS.
    if (now - s_lastRowMs >= CSV_ROW_INTERVAL_MS) {
        s_lastRowMs = now;
        float vals[PID_COUNT];
        bool  present[PID_COUNT];
        obd_get_row(vals, present);
        sdlog_write_csv_row(vals, present);
    }

    // 4) Periodic diagnostics snapshot.
    if (now - s_lastDiagMs >= (uint32_t)DIAG_SNAPSHOT_SEC * 1000UL) {
        s_lastDiagMs = now;
        run_diag_snapshot();
    }

    // 5) Housekeeping (flush + rotate).
    sdlog_service();
    led_service();

    // 6) Serial status every 5 s.
    if (now - s_lastStatusMs >= 5000) {
        s_lastStatusMs = now;
        print_status();
    }

    // 7) CLI.
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (s_cliLen) {
                s_cliBuf[s_cliLen] = 0;
                handle_cli_line(s_cliBuf);
                s_cliLen = 0;
            }
        } else if (s_cliLen < sizeof(s_cliBuf) - 1) {
            s_cliBuf[s_cliLen++] = c;
        }
    }

    // 8) Optional deep sleep when the engine has been off a long time.
#if DEEP_SLEEP_ENABLED
    if (!running && (now - s_lastEngineOnMs) > (uint32_t)ENGINE_OFF_SLEEP_MIN * 60000UL) {
        Serial.println("[PWR] engine off -> deep sleep");
        sdlog_flush();
        led_set(LED_SLEEPING);
        led_service();
        delay(50);
        esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_CHECK_SEC * 1000000ULL);
        esp_deep_sleep_start();
    }
#endif

    // LED colour reflects activity.
    if (!sdlog_card_ok()) {
        led_set(LED_SD_ERROR);
    } else if (running) {
        led_set(LED_RAW_WINDOW);
    } else {
        led_set(LED_LOGGING);
    }

    delay(2);   // let the poller + RX task breathe
}

// ============================================================================
static void run_diag_snapshot()
{
    char line[256];
    uint8_t buf[256];

    // Stored (03), pending (07), permanent (0A) DTCs.
    struct { uint8_t svc; const char *name; } svc[] = {
        { 0x03, "stored" }, { 0x07, "pending" }, { 0x0A, "permanent" },
    };

    for (auto &s : svc) {
        int n = dtc_request(s.svc, buf, sizeof(buf));
        if (n <= 0) continue;
        char codes[24][8];
        int c = dtc_decode(buf, n, codes, 24);
        snprintf(line, sizeof(line), "t=%lu,%s,raw=", (unsigned long)millis(), s.name);
        for (int i = 0; i < n && strlen(line) < 200; i++) {
            char hex[4];
            snprintf(hex, sizeof(hex), "%02X", buf[i]);
            strcat(line, hex);
        }
        sdlog_write_diag(line);
        if (c > 0) {
            for (int i = 0; i < c; i++) {
                snprintf(line, sizeof(line), "t=%lu,%s,DTC,%s",
                         (unsigned long)millis(), s.name, codes[i]);
                sdlog_write_diag(line);
            }
        }
    }

    // Best-effort Mode 06 on-board-monitor scan (raw, TIDs 0x00..0x0F).
    for (uint8_t tid = 0x00; tid <= 0x0F; tid++) {
        int n = mode06_request(tid, buf, sizeof(buf));
        if (n <= 0) continue;
        snprintf(line, sizeof(line), "t=%lu,mode06,%02X,raw=",
                 (unsigned long)millis(), tid);
        for (int i = 0; i < n && strlen(line) < 200; i++) {
            char hex[4];
            snprintf(hex, sizeof(hex), "%02X", buf[i]);
            strcat(line, hex);
        }
        sdlog_write_diag(line);
    }
}

// ============================================================================
static void print_status()
{
    Serial.printf("[STAT] up=%lus eng=%s can=%lu drop=%lu raw=%lluMB csv=%lluMB sid=%u\n",
                  (unsigned long)(millis() / 1000),
                  s_engineWasRunning ? "RUN" : "off",
                  (unsigned long)can_rx_count(),
                  (unsigned long)can_rx_dropped(),
                  (unsigned long long)(sdlog_raw_bytes() / (1024 * 1024)),
                  (unsigned long long)(sdlog_csv_bytes() / (1024 * 1024)),
                  sdlog_session_id());
}

static void print_pids()
{
    float vals[PID_COUNT];
    bool  present[PID_COUNT];
    obd_get_row(vals, present);
    for (size_t i = 0; i < PID_COUNT; i++) {
        Serial.printf("  %-16s %8.3f %-8s %s\n", PID_TABLE[i].label,
                      vals[i], PID_TABLE[i].unit, present[i] ? "" : "(stale)");
    }
}

// ============================================================================
static void handle_cli_line(char *line)
{
    // trim
    while (*line == ' ') line++;
    char *cmd = strtok(line, " ");
    if (!cmd) return;

    if (strcmp(cmd, "help") == 0) {
        Serial.println("commands:");
        Serial.println("  status          - logger status");
        Serial.println("  pids            - live decoded values");
        Serial.println("  time <epoch_s>  - set wall clock base");
        Serial.println("  dtc             - read DTCs now");
        Serial.println("  mode06 <tid_hex>- read Mode 06 test id");
        Serial.println("  flush           - flush SD buffers");
        Serial.println("  reboot          - restart the logger");
    } else if (strcmp(cmd, "status") == 0) {
        print_status();
    } else if (strcmp(cmd, "pids") == 0) {
        print_pids();
    } else if (strcmp(cmd, "time") == 0) {
        char *a = strtok(nullptr, " ");
        if (a) {
            uint64_t epoch_s = strtoull(a, nullptr, 10);
            sdlog_set_epoch(epoch_s * 1000ULL - millis());
            Serial.println("[TIME] base set");
        }
    } else if (strcmp(cmd, "dtc") == 0) {
        run_diag_snapshot();
        Serial.println("[DTC] snapshot written to diag file");
    } else if (strcmp(cmd, "mode06") == 0) {
        char *a = strtok(nullptr, " ");
        uint8_t tid = a ? (uint8_t)strtoul(a, nullptr, 16) : 0x00;
        uint8_t buf[256];
        int n = mode06_request(tid, buf, sizeof(buf));
        Serial.printf("mode06 tid=%02X len=%d : ", tid, n);
        for (int i = 0; i < n; i++) Serial.printf("%02X ", buf[i]);
        Serial.println();
    } else if (strcmp(cmd, "flush") == 0) {
        sdlog_flush();
        Serial.println("[SD] flushed");
    } else if (strcmp(cmd, "reboot") == 0) {
        Serial.println("[SYS] reboot");
        ESP.restart();
    } else {
        Serial.println("unknown command, type 'help'");
    }
}