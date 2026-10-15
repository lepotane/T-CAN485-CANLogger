/*
 * config.h - all tunables for the T-CAN485 ultra-detailed CAN/OBD-II logger.
 *
 * Target vehicle: Mercedes W204 C180K 1.6 ML Kompressor, ECU Siemens/Continental
 * SIM271KE, modified to twincharger (belt-driven supercharger + added exhaust
 * turbo). Goal: capture cold-start misfire behaviour in enough detail to plan
 * the ECU fuel/ignition map corrections.
 *
 * Everything logged to SD card. Device sits in the car 1-2 days, then the card
 * is pulled and the logs are analysed with tools/analyze_logs.py.
 */
#pragma once

#include <stdint.h>

// ================= Identity / timing =================
#define FW_NAME        "T-CAN485-CANLogger"
#define FW_VERSION     "1.0.0"

// TWAI (CAN) bit-rate. W204 CAN-C is 500 kbit/s 11-bit.
#define CAN_BITRATE_KBPS 500

// ================= SD / logging =================
// Raw CAN frame log mode:
//   0 = never log raw frames (CSV only)
//   1 = log raw frames only inside a window after engine start (RAW_WINDOW_SEC)
//   2 = log raw frames continuously while awake  <-- default ("ultra detailed")
#define RAW_MODE            2

// For RAW_MODE == 1: how many seconds of raw frames to keep after each start.
#define RAW_WINDOW_SEC      180

// Rotate raw + CSV files when they reach this size (bytes). Keeps card healthy
// and bounds worst-case growth. 512 MB x many files on a 32 GB card.
#define FILE_ROTATE_BYTES   (512ULL * 1024ULL * 1024ULL)

// How often to write a decoded-PID CSV row (ms). 50 ms = 20 Hz rows.
// The poller refreshes individual PIDs faster than this; each row carries the
// most recent value of every PID.
#define CSV_ROW_INTERVAL_MS 50

// Serial console
#define SERIAL_BAUD         115200

// ================= OBD-II polling =================
// OBD-II physical request address for the engine ECU (ISO 15765-4).
#define OBD_REQ_ID          0x7E0
#define OBD_RESP_ID_BASE    0x7E8   // 0x7E8..0x7EF possible responders

// Per-request response timeout (ms).
#define OBD_RESP_TIMEOUT_MS 120

// ================= Power management =================
// After the engine has been off this long, deep-sleep to save the battery.
// The device runs from OBD pin 16 (constant 12 V) so this matters over 1-2 days.
#define ENGINE_OFF_SLEEP_MIN   8     // minutes of engine-off before deep sleep
#define SLEEP_CHECK_SEC        8     // wake every N s to sniff for CAN activity
#define SLEEP_WAKE_SNIFF_MS    2500  // how long to listen for CAN after waking
// A frame on the bus OR a valid engine RPM counts as "activity".

// Deep sleep is OPTIONAL. Default OFF: the logger stays awake the whole time so
// it never misses the first second of a cold start (the exact event we need).
// Battery cost when awake: ~60-100 mA. Over 1-2 days that is a few Ah - fine for
// a healthy car battery that is also being started. Enable (=1) only if the car
// will sit for many days and you accept possibly missing the first cranks.
#define DEEP_SLEEP_ENABLED     0

// ================= Status LED (WS2812B on GPIO4) =================
#define LED_ENABLED  1

// ================= Live data storage =================
// Values are stored as decoded floats. PID table below defines columns.

// ---- PID decoding helpers -------------------------------------------------
// SAE J1979 Mode 01 decode. Each entry: pid, label, unit, expected data bytes,
// poll interval (ms), and the decode id used by obd2_decode().
//
// Poll intervals:
//   fast set  (50 ms)  : things that change during a cold-start misfire
//   slow set  (250 ms) : temperatures, pressures, voltage, trims

enum ObdGroup : uint8_t {
    GRP_FAST = 0,
    GRP_SLOW = 1,
};

struct PidDef {
    uint8_t     pid;          // Mode 01 PID
    const char *label;        // CSV column name
    const char *unit;
    uint8_t     dataBytes;    // payload bytes after mode+pid in the response
    uint16_t    intervalMs;   // poll interval
    ObdGroup    group;
    uint8_t     decodeId;     // switch-case selector in obd2_decode()
};

// Decode-id constants (keep in sync with obd2_decode()).
enum DecodeId : uint8_t {
    D_LOAD = 1,      // A*100/255                       %
    D_ECT,           // A-40                            degC
    D_STFT,          // (A-128)*100/128                 %
    D_LTFT,          // (A-128)*100/128                 %
    D_FUEL_PRESS,    // A*3                             kPa
    D_MAP,           // A                               kPa
    D_RPM,           // (256A+B)/4                      rpm
    D_SPEED,         // A                               km/h
    D_ADVANCE,       // A/2-64                          deg
    D_IAT,           // A-40                            degC
    D_MAF,           // (256A+B)/100                    g/s
    D_THROTTLE,      // A*100/255                       %
    D_O2_V_STFT,     // A/200 V  ; second byte STFT
    D_O2_V,          // A/200                            V
    D_RUNTIME,       // 256A+B                          s
    D_FUELRAIL_REL,  // (256A+B)*0.079                  kPa
    D_FUELRAIL_ABS,  // (256A+B)*10                     kPa
    D_BARO,          // A                               kPa
    D_CAT_TEMP,      // (256A+B)/10-40                  degC
    D_VOLTAGE,       // (256A+B)/1000                   V
    D_ABSLOAD,       // (256A+B)*100/255                %
    D_EQUIV,         // (256A+B)/32768                  lambda
    D_O2_EQUIV_V,    // (256A+B)/32768 lambda ; (256C+D)/8192 V
    D_OIL_TEMP,      // A-40                            degC
    D_FUELRATE,      // (256A+B)*0.05                   L/h
    D_TORQUE_A,      // A-125                           %
    D_TORQUE_B,      // A-125                           %
    D_TORQUE_REF,    // 256A+B                          Nm
    D_AMBIENT,       // A-40                            degC
};

// Order of this table = column order in the CSV. Keep it stable across sessions
// so the Python analyser can rely on the header.
static const PidDef PID_TABLE[] = {
    // ---- fast set (cold-start critical) ----
    { 0x0C, "rpm",            "rpm",   2,  50, GRP_FAST, D_RPM        },
    { 0x0D, "speed_kmh",      "km/h",  1,  50, GRP_FAST, D_SPEED      },
    { 0x11, "throttle_pct",   "%",     1,  50, GRP_FAST, D_THROTTLE   },
    { 0x49, "pedal_d_pct",    "%",     1,  50, GRP_FAST, D_THROTTLE   },
    { 0x4A, "pedal_e_pct",    "%",     1,  50, GRP_FAST, D_THROTTLE   },
    { 0x10, "maf_gps",        "g/s",   2,  50, GRP_FAST, D_MAF        },
    { 0x0B, "map_kpa",        "kPa",   1,  50, GRP_FAST, D_MAP        },
    { 0x04, "engine_load_pct","%",     1,  50, GRP_FAST, D_LOAD       },
    { 0x43, "abs_load_pct",   "%",     2,  50, GRP_FAST, D_ABSLOAD    },
    { 0x06, "stft_b1_pct",    "%",     1,  50, GRP_FAST, D_STFT       },
    { 0x0E, "timing_adv_deg", "deg",   1,  50, GRP_FAST, D_ADVANCE    },
    { 0x24, "o2_b1s1_equiv",  "lambda",4,  50, GRP_FAST, D_O2_EQUIV_V },
    { 0x44, "cmd_equiv",      "lambda",2,  50, GRP_FAST, D_EQUIV      },

    // ---- slow set (context / thermal / pressure) ----
    { 0x05, "ect_c",          "degC",  1, 250, GRP_SLOW, D_ECT        },
    { 0x0F, "iat_c",          "degC",  1, 250, GRP_SLOW, D_IAT        },
    { 0x46, "ambient_c",      "degC",  1, 250, GRP_SLOW, D_AMBIENT    },
    { 0x5C, "oil_temp_c",     "degC",  1, 250, GRP_SLOW, D_OIL_TEMP   },
    { 0x07, "ltft_b1_pct",    "%",     1, 250, GRP_SLOW, D_LTFT       },
    { 0x08, "stft_b2_pct",    "%",     1, 250, GRP_SLOW, D_STFT       },
    { 0x09, "ltft_b2_pct",    "%",     1, 250, GRP_SLOW, D_LTFT       },
    { 0x0A, "fuel_press_kpa", "kPa",   1, 250, GRP_SLOW, D_FUEL_PRESS },
    { 0x23, "rail_abs_kpa",   "kPa",   2, 250, GRP_SLOW, D_FUELRAIL_ABS },
    { 0x22, "rail_rel_kpa",   "kPa",   2, 250, GRP_SLOW, D_FUELRAIL_REL },
    { 0x2C, "cmd_throttle_pct","%",    1, 250, GRP_SLOW, D_THROTTLE   },
    { 0x47, "throttle_b_pct", "%",     1, 250, GRP_SLOW, D_THROTTLE   },
    { 0x33, "baro_kpa",       "kPa",   1, 250, GRP_SLOW, D_BARO       },
    { 0x3C, "cat_b1s1_c",     "degC",  2, 250, GRP_SLOW, D_CAT_TEMP   },
    { 0x3D, "cat_b1s2_c",     "degC",  2, 250, GRP_SLOW, D_CAT_TEMP   },
    { 0x42, "module_v",       "V",     2, 250, GRP_SLOW, D_VOLTAGE    },
    { 0x25, "o2_b1s2_equiv",  "lambda",4, 250, GRP_SLOW, D_O2_EQUIV_V },
    { 0x1F, "run_time_s",     "s",     2, 250, GRP_SLOW, D_RUNTIME    },
    { 0x5E, "fuel_rate_lph",  "L/h",   2, 250, GRP_SLOW, D_FUELRATE   },
    { 0x61, "drv_torque_pct", "%",     1, 250, GRP_SLOW, D_TORQUE_A   },
    { 0x62, "act_torque_pct", "%",     1, 250, GRP_SLOW, D_TORQUE_B   },
    { 0x63, "ref_torque_nm",  "Nm",    2, 250, GRP_SLOW, D_TORQUE_REF },
};

#define PID_COUNT   (sizeof(PID_TABLE) / sizeof(PID_TABLE[0]))

// ================= Diagnostics snapshot =================
// Read at startup and every DIAG_SNAPSHOT_SEC afterwards.
#define DIAG_SNAPSHOT_SEC   60

// ================= Raw CAN record format =================
// Fixed 18-byte little-endian record written to raw_*.bin after a 16-byte
// file header:
//   header: magic[8]="TC485RAW"  version:u16  flags:u16  session_id:u32
//   record: ts_ms:u32  can_id:u32 (bit31=ext,bit30=rtr)  dlc:u8  res:u8  data[8]
#define RAW_RECORD_LEN   18
#define RAW_MAGIC        "TC485RAW"
#define RAW_FORMAT_VER   0x0001