#include "canbus.h"
#include "pin_config.h"

// ----------------------------------------------------------------------------
// State
// ----------------------------------------------------------------------------
static QueueHandle_t s_rawQueue = nullptr;   // -> SD writer (RawFrame)
static QueueHandle_t s_obdQueue = nullptr;   // -> OBD request/response (twai_message_t)

static volatile uint32_t s_rxCount   = 0;
static volatile uint32_t s_rxDropped = 0;
static volatile uint32_t s_lastRxMs  = 0;

// ISO-TP assembly buffer (one message in flight; OBD is request/response).
static uint8_t  s_asmBuf[4096];
static uint16_t s_asmLen;
static uint16_t s_asmGot;
static bool     s_asmActive;

// ----------------------------------------------------------------------------
// TX helpers
// ----------------------------------------------------------------------------
static bool twai_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    twai_message_t msg = {};
    msg.identifier = id;
    msg.extd = 0;
    msg.rtr = 0;
    msg.ss = 0;
    msg.self = 0;
    msg.dlc_non_comp = 0;
    msg.data_length_code = len;
    memcpy(msg.data, data, len);
    return twai_transmit(&msg, pdMS_TO_TICKS(50)) == ESP_OK;
}

static void send_flow_control()
{
    uint8_t fc[8] = { 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    twai_send(OBD_REQ_ID, fc, 8);
}

bool obd_send_request(uint8_t mode, uint8_t pid)
{
    uint8_t d[8] = { 0x02, mode, pid, 0x00, 0x00, 0x00, 0x00, 0x00 };
    return twai_send(OBD_REQ_ID, d, 8);
}

// Send an arbitrary OBD-II service frame (ISO-TP single frame, no sub-function).
bool obd_send_raw(const uint8_t *data, uint8_t len)
{
    uint8_t d[8];
    memset(d, 0, 8);
    if (len > 7) len = 7;
    d[0] = len;                       // ISO-TP single-frame PCI
    memcpy(&d[1], data, len);
    return twai_send(OBD_REQ_ID, d, 8);
}

// ----------------------------------------------------------------------------
// RX task: route every frame to the raw log queue, and OBD responses to obdQueue.
// ----------------------------------------------------------------------------
static void can_rx_task(void *arg)
{
    (void)arg;
    twai_message_t msg;
    for (;;) {
        if (twai_receive(&msg, pdMS_TO_TICKS(100)) != ESP_OK) {
            continue;
        }

        uint32_t now = millis();
        s_lastRxMs = now;
        s_rxCount++;

        // 1) Always capture for raw logging.
        RawFrame rf;
        rf.tsMs = now;
        rf.id   = msg.identifier;
        if (msg.extd) rf.id |= 0x80000000u;
        if (msg.rtr)  rf.id |= 0x40000000u;
        rf.dlc  = msg.data_length_code;
        memcpy(rf.data, msg.data, 8);
        if (xQueueSend(s_rawQueue, &rf, 0) != pdTRUE) {
            s_rxDropped++;
        }

        // 2) Also forward OBD-II responses to the request/response queue.
        if (!msg.extd && msg.identifier >= OBD_RESP_ID_BASE &&
            msg.identifier <= (OBD_RESP_ID_BASE + 7)) {
            if (xQueueSend(s_obdQueue, &msg, 0) != pdTRUE) {
                s_rxDropped++;
            }
        }
    }
}

// ----------------------------------------------------------------------------
// ISO-TP receive assembly
// ----------------------------------------------------------------------------
int obd_receive_raw(uint8_t *buf, uint16_t maxLen, uint16_t timeoutMs)
{
    uint32_t start = millis();
    twai_message_t msg;

    s_asmActive = false;

    while ((millis() - start) < timeoutMs) {
        uint32_t remaining = timeoutMs - (millis() - start);
        if (remaining == 0) break;

        if (xQueueReceive(s_obdQueue, &msg, pdMS_TO_TICKS(remaining)) != pdTRUE) {
            continue;
        }

        uint8_t pci = msg.data[0];
        uint8_t type = pci >> 4;

        if (type == 0x0) {                       // single frame
            uint8_t len = pci & 0x0F;
            if (len > 7) len = 7;
            if (len > maxLen) len = maxLen;
            memcpy(buf, &msg.data[1], len);
            return len;
        }

        if (type == 0x1) {                       // first frame
            s_asmLen = ((uint16_t)(pci & 0x0F) << 8) | msg.data[1];
            s_asmGot = 0;
            uint8_t chunk = 6;                    // data[2..7]
            if (chunk > sizeof(s_asmBuf)) chunk = sizeof(s_asmBuf);
            memcpy(&s_asmBuf[0], &msg.data[2], chunk);
            s_asmGot = chunk;
            s_asmActive = true;
            send_flow_control();
        } else if (type == 0x2 && s_asmActive) { // consecutive frame
            uint8_t chunk = 7;                    // data[1..7]
            if (s_asmGot + chunk > sizeof(s_asmBuf)) {
                chunk = sizeof(s_asmBuf) - s_asmGot;
            }
            memcpy(&s_asmBuf[s_asmGot], &msg.data[1], chunk);
            s_asmGot += chunk;
        } else {
            continue;                            // flow control / unknown
        }

        if (s_asmActive && s_asmGot >= s_asmLen) {
            uint16_t out = s_asmLen;
            if (out > maxLen) out = maxLen;
            memcpy(buf, s_asmBuf, out);
            s_asmActive = false;
            return out;
        }
    }

    s_asmActive = false;
    return -1;                                   // timeout
}

int obd_receive(uint8_t mode, uint8_t pid, uint8_t *payload, uint8_t maxLen,
                uint16_t timeoutMs)
{
    uint8_t msg[512];
    int n = obd_receive_raw(msg, sizeof(msg), timeoutMs);
    if (n < 2) return -1;
    if (msg[0] != (uint8_t)(mode + 0x40) || msg[1] != pid) return -1;
    int plen = n - 2;
    if (plen > maxLen) plen = maxLen;
    memcpy(payload, &msg[2], plen);
    return plen;
}

// ----------------------------------------------------------------------------
// Raw queue consumer (called by SD writer)
// ----------------------------------------------------------------------------
bool can_pop_raw(RawFrame &out, uint32_t waitMs)
{
    return xQueueReceive(s_rawQueue, &out, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

bool can_bus_active(uint32_t withinMs)
{
    return (millis() - s_lastRxMs) < withinMs;
}

uint32_t can_rx_count()   { return s_rxCount; }
uint32_t can_rx_dropped() { return s_rxDropped; }

// ----------------------------------------------------------------------------
// Init
// ----------------------------------------------------------------------------
bool canbus_begin()
{
    // Power the shared CAN/RS485 transceiver boost regulator.
    pinMode(ME2107_EN, OUTPUT);
    digitalWrite(ME2107_EN, HIGH);
    delay(20);

    // Select high-speed CAN mode.
    pinMode(CAN_SPEED_MODE, OUTPUT);
    digitalWrite(CAN_SPEED_MODE, LOW);
    delay(5);

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, TWAI_MODE_NORMAL);
    g.tx_queue_len = 64;
    g.rx_queue_len = 2048;

    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();

    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g, &t, &f) != ESP_OK) {
        Serial.println("[CAN] driver install FAILED");
        return false;
    }
    if (twai_start() != ESP_OK) {
        Serial.println("[CAN] start FAILED");
        return false;
    }

    uint32_t alerts = TWAI_ALERT_TX_IDLE | TWAI_ALERT_TX_SUCCESS |
                      TWAI_ALERT_TX_FAILED | TWAI_ALERT_ERR_PASS |
                      TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_DATA |
                      TWAI_ALERT_RX_QUEUE_FULL;
    twai_reconfigure_alerts(alerts, nullptr);

    s_rawQueue = xQueueCreate(4096, sizeof(RawFrame));
    s_obdQueue = xQueueCreate(256, sizeof(twai_message_t));
    if (!s_rawQueue || !s_obdQueue) {
        Serial.println("[CAN] queue alloc FAILED");
        return false;
    }

    xTaskCreatePinnedToCore(can_rx_task, "can_rx", 4096, nullptr, 3, nullptr, 1);

    Serial.printf("[CAN] up at %d kbit/s (TX=%d RX=%d)\n",
                  CAN_BITRATE_KBPS, CAN_TX, CAN_RX);
    return true;
}