#pragma once
#include <Arduino.h>

enum LedState : uint8_t {
    LED_BOOT = 0,
    LED_LOGGING,
    LED_RAW_WINDOW,
    LED_SD_ERROR,
    LED_SLEEPING,
    LED_NO_ECU,
};

void led_begin();
void led_set(LedState s);
void led_service();   // call often (handles blink)