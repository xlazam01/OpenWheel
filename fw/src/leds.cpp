#include "leds.h"
#include <HardwareTimer.h>
#include "config.h"

static const uint32_t PWM_TICK_HZ = 12800;   // 64 steps -> 200 Hz, no visible flicker
static const uint8_t  PWM_STEPS   = 64;

static HardwareTimer* pwmTimer = nullptr;
static volatile uint32_t onMask = 0;       // GPIOB bits of the LEDs that should be lit
static volatile uint8_t duty = PWM_STEPS;  // lit ticks per period
static volatile uint8_t phase = 0;
static uint8_t brightness = 100;
static uint32_t bitLed1, bitLed2;          // GPIOB pin masks, set in ledsBegin()

static uint32_t ledBit(StatusLed led) {
    return led == LED1 ? bitLed1 : bitLed2;
}

static void pwmTick() {
    uint32_t all = bitLed1 | bitLed2;
    uint32_t lit = (phase < duty) ? onMask : 0;
    // active low: reset (BR, upper half) = on, set (BS, lower half) = off
    GPIOB->BSRR = (lit << 16) | (all & ~lit);
    phase = (phase + 1) % PWM_STEPS;
}

void ledsBegin() {
    bitLed1 = digitalPinToBitMask(PIN_STATUS_LED1);
    bitLed2 = digitalPinToBitMask(PIN_STATUS_LED2);
    pinMode(PIN_STATUS_LED1, OUTPUT);
    pinMode(PIN_STATUS_LED2, OUTPUT);
    digitalWrite(PIN_STATUS_LED1, HIGH);
    digitalWrite(PIN_STATUS_LED2, HIGH);
    pwmTimer = new HardwareTimer(TIM16);
    pwmTimer->setOverflow(PWM_TICK_HZ, HERTZ_FORMAT);
    pwmTimer->attachInterrupt(pwmTick);
}

void ledSet(StatusLed led, bool on) {
    uint32_t bit = ledBit(led);
    if (on) onMask |= bit; else onMask &= ~bit;
    if (brightness == 100) {
        digitalWrite(led == LED1 ? PIN_STATUS_LED1 : PIN_STATUS_LED2, on ? LOW : HIGH);
    }
}

void ledsSetBrightness(uint8_t percent) {
    if (percent > 100) percent = 100;
    brightness = percent;
    // gamma 2: equal steps look equally bright; anything above 0 % gets at least one tick
    uint32_t d = ((uint32_t)percent * percent * PWM_STEPS + 5000) / 10000;
    if (percent > 0 && d == 0) d = 1;
    duty = (uint8_t)d;
    if (pwmTimer == nullptr) return;
    if (percent == 100) {
        pwmTimer->pause();
        digitalWrite(PIN_STATUS_LED1, (onMask & ledBit(LED1)) ? LOW : HIGH);
        digitalWrite(PIN_STATUS_LED2, (onMask & ledBit(LED2)) ? LOW : HIGH);
    } else {
        pwmTimer->resume();
    }
}
