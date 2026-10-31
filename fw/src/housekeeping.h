// Housekeeping measurements on the MCU ADC: chip temperature, VDDA and the +5V rail.
// All readings use the factory calibration (VREFINT, TS_CAL1/TS_CAL2), so they do not
// depend on the 3.3 V regulator tolerance.
#pragma once
#include <Arduino.h>
#include "config.h"

class Housekeeping {
public:
    void begin();
    // Call every main loop iteration. While the motor runs only the +5V rail is sampled (fast);
    // otherwise everything is sampled every HK_IDLE_MS.
    void loop(bool motorRunning);
    // Restart the "minimum since the last move started" tracking
    void motionStarted();

    int16_t mcuTempDeciC() const { return mcuTemp; }   // 0.1 degC
    uint16_t vddaMv() const { return vdda; }
    int16_t v5Mv() const { return v5; }                // -1 if the board has no sense divider
    int16_t v5MinMv() const { return v5Min; }          // minimum since the last move started, -1 if none
    bool v5Low() const;

private:
    void sampleAll();
    void sampleV5();

    int16_t mcuTemp = 0;
    uint16_t vdda = 3300;
    int16_t v5 = -1;
    int16_t v5Min = -1;
    unsigned long lastSample = 0;
};

extern Housekeeping housekeeping;
