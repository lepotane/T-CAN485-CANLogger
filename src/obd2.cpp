#include "obd2.h"
#include "canbus.h"

// ----------------------------------------------------------------------------
// Shared snapshot
// ----------------------------------------------------------------------------
static ObdSnapshot s_snap;
static SemaphoreHandle_t s_mutex = nullptr;
static SemaphoreHandle_t s_txn = nullptr;   // serializes OBD request/response

// ----------------------------------------------------------------------------
// Decode
// ----------------------------------------------------------------------------
float obd_decode(uint8_t decodeId, const uint8_t *d, bool *ok, float *extra)
{
    if (ok) *ok = true;
    if (extra) *extra = 0.0f;
    uint16_t A = d[0], B = d[1], C = d[2], D = d[3];

    switch (decodeId) {
        case D_LOAD:        return A * 100.0f / 255.0f;
        case D_ECT:         return (float)A - 40.0f;
        case D_STFT:
        case D_LTFT:        return ((float)A - 128.0f) * 100.0f / 128.0f;
        case D_FUEL_PRESS:  return A * 3.0f;
        case D_MAP:         return (float)A;
        case D_RPM:         return (256.0f * A + B) / 4.0f;
        case D_SPEED:       return (float)A;
        case D_ADVANCE:     return A / 2.0f - 64.0f;
        case D_IAT:
        case D_AMBIENT:
        case D_OIL_TEMP:    return (float)A - 40.0f;
        case D_MAF:         return (256.0f * A + B) / 100.0f;
        case D_THROTTLE:    return A * 100.0f / 255.0f;
        case D_O2_V_STFT:
            if (extra) *extra = ((float)B - 128.0f) * 100.0f / 128.0f;
            return A / 200.0f;
        case D_O2_V:        return A / 200.0f;
        case D_RUNTIME:     return 256.0f * A + B;
        case D_FUELRAIL_REL:return (256.0f * A + B) * 0.079f;
        case D_FUELRAIL_ABS:return (256.0f * A + B) * 10.0f;
        case D_BARO:        return (float)A;
        case D_CAT_TEMP:    return (256.0f * A + B) / 10.0f - 40.0f;
        case D_VOLTAGE:     return (256.0f * A + B) / 1000.0f;
        case D_ABSLOAD:     return (256.0f * A + B) * 100.0f / 255.0f;
        case D_EQUIV:       return (256.0f * A + B) / 32768.0f;
        case D_O2_EQUIV_V:
            if (extra) *extra = (256.0f * C + D) / 8192.0f;
            return (256.0f * A + B) / 32768.0f;
        case D_FUELRATE:    return (256.0f * A + B) * 0.05f;
        case D_TORQUE_A:
        case D_TORQUE_B:    return (float)A - 125.0f;
        case D_TORQUE_REF:  return 256.0f * A + B;
        default:
            if (ok) *ok = false;
            return 0.0f;
    }
}

// ----------------------------------------------------------------------------
// Snapshot access
// ----------------------------------------------------------------------------
void obd_get_snapshot(ObdSnapshot &out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    out = s_snap;
    xSemaphoreGive(s_mutex);
}

void obd_get_row(float *out, bool *present)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (size_t i = 0; i < PID_COUNT; i++) {
        out[i] = s_snap.value[i];
        present[i] = s_snap.valid[i];
    }
    xSemaphoreGive(s_mutex);
}

bool obd_engine_running()
{
    for (size_t i = 0; i < PID_COUNT; i++) {
        if (PID_TABLE[i].pid == 0x0C) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            bool v = s_snap.valid[i] &&
                     s_snap.value[i] > 150.0f &&
                     (millis() - s_snap.ts[i]) < 2000;
            xSemaphoreGive(s_mutex);
            return v;
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
// Poller
// ----------------------------------------------------------------------------
static void poll_one(size_t i)
{
    uint8_t payload[16];
    xSemaphoreTake(s_txn, portMAX_DELAY);
    if (!obd_send_request(0x01, PID_TABLE[i].pid)) {
        xSemaphoreGive(s_txn);
        return;
    }
    int n = obd_receive(0x01, PID_TABLE[i].pid, payload, sizeof(payload));
    xSemaphoreGive(s_txn);
    if (n < (int)PID_TABLE[i].dataBytes) return;

    bool ok = false;
    float extra = 0.0f;
    float val = obd_decode(PID_TABLE[i].decodeId, payload, &ok, &extra);
    if (!ok) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_snap.value[i] = val;
    s_snap.valid[i] = true;
    s_snap.ts[i] = millis();
    xSemaphoreGive(s_mutex);
}

static void obd_poll_task(void *arg)
{
    (void)arg;
    uint32_t last[PID_COUNT] = {0};
    for (;;) {
        uint32_t now = millis();
        for (size_t i = 0; i < PID_COUNT; i++) {
            if (now - last[i] >= PID_TABLE[i].intervalMs) {
                poll_one(i);
                last[i] = now;
                now = millis();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void obd_begin()
{
    s_mutex = xSemaphoreCreateMutex();
    s_txn   = xSemaphoreCreateMutex();
    memset(&s_snap, 0, sizeof(s_snap));
    xTaskCreatePinnedToCore(obd_poll_task, "obd_poll", 8192, nullptr, 2, nullptr, 1);
    Serial.println("[OBD] poller started");
}

// ----------------------------------------------------------------------------
// Diagnostics
// ----------------------------------------------------------------------------
int dtc_request(uint8_t service, uint8_t *buf, uint16_t maxLen)
{
    uint8_t req = service;
    xSemaphoreTake(s_txn, portMAX_DELAY);
    bool ok = obd_send_raw(&req, 1);
    int n = ok ? obd_receive_raw(buf, maxLen) : -1;
    xSemaphoreGive(s_txn);
    return n;
}

int mode06_request(uint8_t tid, uint8_t *buf, uint16_t maxLen)
{
    xSemaphoreTake(s_txn, portMAX_DELAY);
    bool ok = obd_send_request(0x06, tid);
    int n = ok ? obd_receive_raw(buf, maxLen) : -1;
    xSemaphoreGive(s_txn);
    return n;
}

int dtc_decode(const uint8_t *payload, int len, char out[][8], int maxCodes)
{
    // payload[0] == service response (0x43/0x47/0x4A). DTCs are 2 bytes each.
    int start = 1;
    // Some ECUs prepend a count byte when the total length is odd.
    if (((len - 1) & 1) != 0) start = 2;

    int count = 0;
    for (int i = start; i + 1 < len && count < maxCodes; i += 2) {
        uint8_t b0 = payload[i];
        uint8_t b1 = payload[i + 1];
        if (b0 == 0 && b1 == 0) continue;
        char letter = "PCBU"[(b0 >> 6) & 0x03];
        uint8_t d1 = (b0 >> 4) & 0x03;
        uint8_t d2 = b0 & 0x0F;
        uint8_t d3 = (b1 >> 4) & 0x0F;
        uint8_t d4 = b1 & 0x0F;
        snprintf(out[count], 8, "%c%X%X%X%X", letter, d1, d2, d3, d4);
        count++;
    }
    return count;
}