// Status LEDs (active low on PB12 / PB4) with brightness control.
// Neither pin has a usable PWM channel (PB4 is on TIM3, which runs the stepper), so dimming is a
// software PWM from TIM16. At 100 % the timer is stopped and the pins are driven directly.
#pragma once
#include <Arduino.h>

enum StatusLed : uint8_t { LED1 = 0, LED2 = 1 };

void ledsBegin();
void ledSet(StatusLed led, bool on);
// 0..100 %, perceptual (gamma) scale; 0 = LEDs always off
void ledsSetBrightness(uint8_t percent);
