#pragma once

#include <Arduino.h>
#include "driver/twai.h"
#include "config.h"

// One decoded raw CAN frame ready for logging / routing.
struct RawFrame {
    uint32_t tsMs;     // ms since logger session start
    uint32_t id;       // bit31 = extended, bit30 = rtr
    uint8_t  dlc;
    uint8_t  data[8];
};

// Bring up TWAI at CAN_BITRATE_KBPS, power the transceiver, start RX task.
bool canbus_begin();

// Send an OBD-II request (ISO-TP single frame) to OBD_REQ_ID.
bool obd_send_request(uint8_t mode, uint8_t pid);

// Send an arbitrary OBD-II service frame (used for Mode 03/07/0A which have
// no sub-function byte). `data`/`len` is the ISO-TP payload, len<=7.
bool obd_send_raw(const uint8_t *data, uint8_t len);

// Blocking-ish: wait for an OBD response matching (mode,pid) and copy payload.
// Returns number of payload bytes (after mode+pid) or -1 on timeout/error.
int  obd_receive(uint8_t mode, uint8_t pid, uint8_t *payload, uint8_t maxLen,
                 uint16_t timeoutMs = OBD_RESP_TIMEOUT_MS);

// Diagnostics (Mode 03/07/0A) and Mode 06 use a slightly different response
// layout; these helpers return the full ISO-TP assembled message.
int  obd_receive_raw(uint8_t *buf, uint16_t maxLen,
                     uint16_t timeoutMs = OBD_RESP_TIMEOUT_MS);

// Pop the next raw frame captured by the sniffer (non-OBD + OBD alike).
bool can_pop_raw(RawFrame &out, uint32_t waitMs);

// True when the sniffer has seen recent bus traffic (used for sleep logic).
bool can_bus_active(uint32_t withinMs = 2000);

uint32_t can_rx_count();
uint32_t can_rx_dropped();