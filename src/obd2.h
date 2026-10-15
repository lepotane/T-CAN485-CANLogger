#pragma once

#include <Arduino.h>
#include "config.h"

// Decoded snapshot of every PID in PID_TABLE, refreshed by the poller task.
struct ObdSnapshot {
    float    value[PID_COUNT];
    bool     valid[PID_COUNT];
    uint32_t ts[PID_COUNT];
};

// Start the polling task (fills the shared snapshot).
void obd_begin();

// Copy the current snapshot under lock.
void obd_get_snapshot(ObdSnapshot &out);

// Copy one CSV row (in PID_TABLE column order) into `out`; each entry is the
// latest decoded value. `present[i]` tells the writer which are fresh enough.
void obd_get_row(float *out, bool *present);

bool obd_engine_running();          // rpm above threshold and fresh

// ---- Diagnostics -----------------------------------------------------------
// Decode DTCs from a Mode 03/07/0A response payload into strings.
int  dtc_decode(const uint8_t *payload, int len, char out[][8], int maxCodes);

// Blocking snapshot of a diagnostic service. Returns response length or -1.
int  dtc_request(uint8_t service, uint8_t *buf, uint16_t maxLen);

// Mode 06 on-board monitoring: request a single Test ID and return raw bytes.
int  mode06_request(uint8_t tid, uint8_t *buf, uint16_t maxLen);

// Decode the raw CAN payload of a Mode 01 PID (exposed for tests/tools).
float obd_decode(uint8_t decodeId, const uint8_t *d, bool *ok, float *extra);