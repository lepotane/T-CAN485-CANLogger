#include "led.h"
#include "config.h"
#include "pin_config.h"
#include <Arduino.h>
#include <soc/gpio_struct.h>   // direct GPIO register access (fast WS2812B timing)

#if LED_ENABLED
static uint32_t s_mask = 0;

// WS2812B timing at 240 MHz (1 cycle = 4.167 ns):
//   T0H ~0.40 us = 96 cyc    T1H ~0.80 us = 192 cyc
//   T0L ~0.85 us = 204 cyc   T1L ~0.45 us = 108 cyc
#define CYC_T0H  96
#define CYC_T1H  192
#define CYC_T0L  204
#define CYC_T1L  108

static inline void delayCycles(uint32_t cyc)
{
    uint32_t start = ESP.getCycleCount();
    while ((ESP.getCycleCount() - start) < cyc) { /* spin */ }
}

static inline void pinHigh() { GPIO.out_w1ts = s_mask; }
static inline void pinLow()  { GPIO.out_w1tc = s_mask; }

static void sendByte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        if ((b >> i) & 1) {
            pinHigh(); delayCycles(CYC_T1H);
            pinLow();  delayCycles(CYC_T1L);
        } else {
            pinHigh(); delayCycles(CYC_T0H);
            pinLow();  delayCycles(CYC_T0L);
        }
    }
}

static void showColor(uint8_t r, uint8_t g, uint8_t b)
{
    portDISABLE_INTERRUPTS();
    sendByte(g);     // GRB order
    sendByte(r);
    sendByte(b);
    portENABLE_INTERRUPTS();
    delayMicroseconds(70);   // latch / reset
}
#endif

static LedState s_state = LED_BOOT;

void led_begin()
{
#if LED_ENABLED
    s_mask = 1UL << WS2812B_DATA;
    pinMode(WS2812B_DATA, OUTPUT);
    pinLow();
    showColor(0, 0, 40);     // blue = booting
#endif
}

void led_set(LedState s) { s_state = s; }

void led_service()
{
#if LED_ENABLED
    static uint32_t last = 0;
    if (millis() - last < 150) return;   // rate-limit
    last = millis();

    uint8_t r = 0, g = 0, b = 0;
    bool blink = false;
    switch (s_state) {
        case LED_BOOT:       r = 0;   g = 0;   b = 40; break;
        case LED_LOGGING:    r = 0;   g = 40;  b = 0;  break;
        case LED_RAW_WINDOW: r = 0;   g = 35;  b = 35; break;
        case LED_SD_ERROR:   r = 40;  g = 0;   b = 0;  blink = true; break;
        case LED_SLEEPING:   r = 30;  g = 0;   b = 30; break;
        case LED_NO_ECU:     r = 35;  g = 18;  b = 0;  blink = true; break;
    }
    if (blink && ((millis() / 300) & 1)) { r = g = b = 0; }
    showColor(r, g, b);
#endif
}