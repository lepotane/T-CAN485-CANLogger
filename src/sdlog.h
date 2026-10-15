#pragma once

#include <Arduino.h>
#include "canbus.h"
#include "config.h"

// Mount SD, create/continue the session folder, open the first file set.
bool sdlog_begin();

// Set the wall-clock base so CSV/rows can carry unix ms. baseUnixMs is the epoch
// (ms) corresponding to millis()==0. 0 = unknown (rows carry 0).
void sdlog_set_epoch(uint64_t baseUnixMs);

// RAW_MODE 1 windowing: call with true when the engine starts, false when off.
void sdlog_set_engine_state(bool running);

// Append one raw frame record (respects RAW_MODE).
void sdlog_write_raw(const RawFrame &f);

// Append one decoded PID row (columns follow PID_TABLE order).
void sdlog_write_csv_row(const float *vals, const bool *present);

// Append a free-form line to the diagnostics file.
void sdlog_write_diag(const char *line);

// Periodic housekeeping: flush buffers, rotate files, RAW_MODE 1 window logic.
void sdlog_service();

void sdlog_flush();

// Info / status
uint32_t sdlog_session_id();
uint32_t sdlog_part();
uint64_t sdlog_raw_bytes();
uint64_t sdlog_csv_bytes();
bool     sdlog_card_ok();