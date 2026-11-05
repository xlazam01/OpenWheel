#include "housekeeping.h"

Housekeeping housekeeping;

static const uint32_t ADC_FULL = 4095;      // 12 bit
static const uint32_t CAL_VDDA_MV = 3000;   // VREFINT_CAL / TS_CAL are taken at VDDA = 3.0 V

void Housekeeping::begin() {
    analogReadResolution(12);
#if HAS_V5_SENSE
    pinMode(PIN_V5_SENSE, INPUT_ANALOG);
#endif
    sampleAll();
}

void Housekeeping::loop(bool motorRunning) {
    unsigned long now = millis();
    if (motorRunning) {
#if HAS_V5_SENSE
        if (now - lastSample >= HK_MOVE_MS) {
            lastSample = now;
            sampleV5();
        }
#endif
    } else if (now - lastSample >= HK_IDLE_MS) {
        lastSample = now;
        sampleAll();
    }
}

void Housekeeping::motionStarted() {
    v5Min = v5;
}

bool Housekeeping::v5Low() const {
    return (v5 >= 0 && v5 < V5_LOW_MV) || (v5Min >= 0 && v5Min < V5_LOW_MV);
}

void Housekeeping::sampleAll() {
    // Average 16 conversions of each internal channel (single readings jitter by about +-1 %)
    uint32_t ref = 0;
    for (uint8_t i = 0; i < 16; i++) ref += analogRead(AVREF);
    if (ref > 0) {
        vdda = (uint16_t)((uint32_t)(*VREFINT_CAL_ADDR) * CAL_VDDA_MV * 16 / ref);
    }

    // Scale the temperature reading to the 3.0 V calibration conditions
    uint32_t raw = 0;
    for (uint8_t i = 0; i < 16; i++) raw += analogRead(ATEMP);
    int32_t ts = (int32_t)(raw * vdda / CAL_VDDA_MV / 16);
    int32_t cal1 = *TEMPSENSOR_CAL1_ADDR;
    int32_t cal2 = *TEMPSENSOR_CAL2_ADDR;
    if (cal2 != cal1) {
        mcuTemp = (int16_t)((ts - cal1) * (TEMPSENSOR_CAL2_TEMP - TEMPSENSOR_CAL1_TEMP) * 10 / (cal2 - cal1) +
                            TEMPSENSOR_CAL1_TEMP * 10);
    }

#if HAS_V5_SENSE
    sampleV5();
#endif
}

void Housekeeping::sampleV5() {
    uint32_t raw = analogRead(PIN_V5_SENSE);
    v5 = (int16_t)(raw * vdda * V5_DIVIDER / ADC_FULL);
    if (v5Min < 0 || v5 < v5Min) v5Min = v5;
}
