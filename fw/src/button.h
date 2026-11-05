// Button handler with single click, double click, and long press detection
#pragma once
#include <Arduino.h>

class Button {
public:
    Button(uint8_t pin, bool activeLow = true);
    void begin();
    void loop();

    // Check button events (clears the flag after reading)
    bool wasSingleClick();
    bool wasDoubleClick();
    bool wasLongPress();

private:
    uint8_t pin;
    bool activeLow;  // true = active LOW, false = active HIGH

    // Debounced button state
    bool buttonPressed;
    bool lastButtonState;
    unsigned long lastDebounceTime;
    bool lastRawState;

    // Event timing
    unsigned long pressStartTime;
    unsigned long firstClickReleaseTime;

    // Event flags
    bool singleClickFlag;
    bool doubleClickFlag;
    bool longPressFlag;

    // State tracking
    bool waitingForSecondClick;
    bool longPressReported;

    // Timing constants (in milliseconds)
    static const unsigned long DEBOUNCE_TIME = 50;
    static const unsigned long LONG_PRESS_TIME = 1000;
    static const unsigned long DOUBLE_CLICK_WINDOW = 500;
};
