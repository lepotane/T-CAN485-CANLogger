/*
 * LILYGO T-CAN485 pin map - taken from the official Xinyuan-LilyGO/T-CAN485
 * repository (lib/Mylibrary/pin_config.h). Do not change unless your board
 * revision differs.
 */
#pragma once

// RS485
#define RS485_TX 22
#define RS485_RX 21
#define RS485_CALLBACK 17
#define RS485_EN 19

// WS2812B status LED
#define WS2812B_DATA 4

// CAN (TWAI)
#define CAN_TX 27
#define CAN_RX 26
#define CAN_SPEED_MODE 23      // LOW = high-speed CAN mode (500 kbit/s)

// Shared boost power supply enable for RS485 + CAN transceivers.
// MUST be driven HIGH or the CAN transceiver stays unpowered.
#define ME2107_EN 16

// SD card (SPI)
#define SD_MISO 2
#define SD_MOSI 15
#define SD_SCLK 14
#define SD_CS 13