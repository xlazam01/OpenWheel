// Button handler implementation
#include "button.h"

Button::Button(uint8_t pin, bool activeLow) :
    pin(pin),
    activeLow(activeLow),
    buttonPressed(false),
    lastButtonState(false),
    lastDebounceTime(0),
    lastRawState(activeLow ? HIGH : LOW),
    pressStartTime(0),
    firstClickReleaseTime(0),
    singleClickFlag(false),
    doubleClickFlag(false),
    longPressFlag(false),
    waitingForSecondClick(false),
    longPressReported(false) {
}

void Button::begin() {
    if (activeLow) {
        pinMode(pin, INPUT_PULLUP);  // Active LOW (pressed = LOW)
    } else {
        pinMode(pin, INPUT_PULLDOWN);  // Active HIGH (pressed = HIGH)
    }
}

void Button::loop() {
    unsigned long now = millis();
    bool rawState = digitalRead(pin);

    // Debounce the button input
    if (rawState != lastRawState) {
        lastDebounceTime = now;
    }

    bool debouncedPressed = false;
    if ((now - lastDebounceTime) > DEBOUNCE_TIME) {
        // State has been stable for DEBOUNCE_TIME
        if (activeLow) {
            debouncedPressed = (rawState == LOW);  // Active LOW: pressed when pin is LOW
        } else {
            debouncedPressed = (rawState == HIGH);  // Active HIGH: pressed when pin is HIGH
        }
    } else {
        // Still bouncing, keep previous state
        debouncedPressed = buttonPressed;
    }

    // Detect press edge (button just pressed)
    if (debouncedPressed && !lastButtonState) {
        pressStartTime = now;
        longPressReported = false;
    }

    // Detect release edge (button just released)
    if (!debouncedPressed && lastButtonState) {
        unsigned long pressDuration = now - pressStartTime;

        // Only process if not a long press
        if (pressDuration < LONG_PRESS_TIME && !longPressReported) {
            if (waitingForSecondClick) {
                // This is the second click - double click detected
                doubleClickFlag = true;
                waitingForSecondClick = false;
            } else {
                // This is the first click - wait for possible second click
                waitingForSecondClick = true;
                firstClickReleaseTime = now;
            }
        }
    }

    // Check for long press while button is held
    if (debouncedPressed && !longPressReported) {
        unsigned long pressDuration = now - pressStartTime;
        if (pressDuration >= LONG_PRESS_TIME) {
            longPressFlag = true;
            longPressReported = true;
            waitingForSecondClick = false;  // Cancel any pending click
        }
    }

    // Check for single click timeout (waiting for second click that never came)
    if (waitingForSecondClick && (now - firstClickReleaseTime) > DOUBLE_CLICK_WINDOW) {
        singleClickFlag = true;
        waitingForSecondClick = false;
    }

    lastButtonState = debouncedPressed;
    buttonPressed = debouncedPressed;
    lastRawState = rawState;
}

bool Button::wasSingleClick() {
    if (singleClickFlag) {
        singleClickFlag = false;
        return true;
    }
    return false;
}

bool Button::wasDoubleClick() {
    if (doubleClickFlag) {
        doubleClickFlag = false;
        return true;
    }
    return false;
}

bool Button::wasLongPress() {
    if (longPressFlag) {
        longPressFlag = false;
        return true;
    }
    return false;
}
