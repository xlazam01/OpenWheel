// FilterWheel implementation with state machine
#include "filterwheel.h"
#include "stepper_timer.h"

#include "protocol_openwheel.h"

// Timeouts
static const unsigned long MOVE_TIMEOUT_MS       = 30000;
static const unsigned long HOME_TIMEOUT_MS       = 30000;
static const unsigned long CALIBRATE_TIMEOUT_MS  = 90000;
static const unsigned long ERROR_RECOVER_MS      = 10000;  // ERROR state shown this long, then back to IDLE
static const unsigned long DRIVER_CHECK_MS       = 2000;   // idle TMC2209 health check interval

// TMC2209 DRV_STATUS fault bits: ot, s2ga, s2gb, s2vsa, s2vsb
static const uint32_t TMC_FAULT_MASK = 0x0000003E;

// External C function for bootloader jump
extern "C" void JumpToBootloader(void);

// Global hardware objects
HardwareSerial  Serial2(USART2);
TMC2209Stepper  driver(&Serial2, R_SENSE, DRIVER_ADDRESS);
// StepperTimer stepper is now defined in stepper_timer.cpp
HallSensor      hallSensor(PIN_HALL, PIN_STATUS_LED2);

FilterWheel::FilterWheel() :
    state(STATE_INIT),
    currentSlot(0),
    targetSlot(0),
    lastError(FW_OK),
    positionValid(false),
    eepromFault(false),
    pulsesSeen(0),
    lastDriverCheck(0),
    inPulse(false),
    pulseStartPosition(0),
    stateStartTime(0),
    applyingOffset(false),
    stepMovementMode(false),
    blinkingSlotNumber(false),
    blinksRemaining(0),
    lastBlinkTime(0),
    blinkLedState(false),
    calibrationPass(0),
    button(PIN_BUTTON, true),        // CW button: active LOW
    buttonCCW(PIN_BUTTON_CCW, false), // CCW button: active HIGH (inverted logic)
    buttonPressedDuringBoot(false),
    protocol(nullptr) {
}

void FilterWheel::init_hardware() {
    // Configure step/dir/enable/diag pins
    pinMode(EN_PIN, OUTPUT);
    pinMode(DIR_PIN, OUTPUT);
    pinMode(STEP_PIN, OUTPUT);
    pinMode(DIAG_PIN, INPUT);

    // Enable driver (EN active low) - keep enabled all the time
    digitalWrite(EN_PIN, LOW);

    // Configure LED pins (active LOW)
    pinMode(PIN_STATUS_LED1, OUTPUT);
    pinMode(PIN_STATUS_LED2, OUTPUT);
    digitalWrite(PIN_STATUS_LED1, HIGH);  // OFF (active low)
    digitalWrite(PIN_STATUS_LED2, HIGH);  // OFF (active low)

    // Start UART used by TMC2209
    Serial2.begin(115200);

    // Initialize TMC driver over UART
    driver.begin();
    driver.toff(TMC_TOFF);
    driver.rms_current(DEFAULT_RUN_CURRENT_MA);
    driver.microsteps(TMC_MICROSTEPS);
    driver.pwm_autoscale(TMC_PWM_AUTOSCALE);

    // Set hold current to reduce power consumption when idle
    driver.hold_multiplier(TMC_HOLD_MULTIPLIER);

    // Set rotation direction based on configuration
#if ROTATION_DIRECTION == ROTATION_DIRECTION_CCW
    driver.shaft(0);  // CCW
    DEBUG_PRINTLN("  Motor direction: Counter-clockwise");
#else
    driver.shaft(1);  // CW
    DEBUG_PRINTLN("  Motor direction: Clockwise");
#endif

    // Initialize timer-based stepper
    stepper.begin();
    stepper.setCurrentPosition(0);
    stepper.setMaxSpeed(STEPPER_MAX_SPEED);
    stepper.setAcceleration(STEPPER_ACCEL);

       
    // Initialize EEPROM
    DEBUG_PRINT("  Initializing EEPROM... ");
    eeprom.setMemoryType(64);
    eeprom.setMemorySizeBytes(EEPROMSIZE);
    eeprom.setPageSizeBytes(32);
    eeprom.setAddressBytes(2);

    if (eeprom.begin(EEPROM_ADDRESS, Wire)) {
        DEBUG_PRINTLN("OK");
    } else {
        DEBUG_PRINTLN("FAILED!");
        eepromFault = true;
        lastError = FW_ERR_EEPROM;
    }

    // TMC2209 must answer on UART; otherwise report it (sticky error, state is left alone
    // so the host can still talk to the device and read the cause)
    if (!checkDriver()) {
        lastError = FW_ERR_MOTOR_DRIVER;
    }
}

void FilterWheel::start() {
    init_hardware();
    hallSensor.start();
    button.begin();
    buttonCCW.begin();

    // Check if button is pressed during boot
    delay(100);  // Small delay to stabilize button reading
    buttonPressedDuringBoot = (digitalRead(PIN_BUTTON) == LOW);

    if (buttonPressedDuringBoot) {
        DEBUG_PRINTLN("Button pressed during boot - will calibrate after init");
    }

    // Initialize protocol handler
    protocol = new ProtocolOpenWheel(this);
    DEBUG_PRINTLN("Protocol: OpenWheel");

    state = STATE_INIT;
    stateStartTime = millis();
}

void FilterWheel::loop() {
    // Update stepper motion profile (must be called frequently)
    stepper.update();

    // Process serial protocol commands
    if (protocol != nullptr) {
        protocol->process();
    }

    // Update hall sensor and buttons
    hallSensor.loop();
    button.loop();
    buttonCCW.loop();

    // Handle button events (only when IDLE)
    handleButtonEvents();

    // Update LED Status1 and Status2 based on state
    static unsigned long lastBlinkTime = 0;
    static bool led1State = false;

    if (state == STATE_ERROR) {
        // ERROR: Both LEDs blink alternately (100ms interval)
        if (millis() - lastBlinkTime > 100) {
            led1State = !led1State;
            digitalWrite(PIN_STATUS_LED1, led1State ? LOW : HIGH);      // Active LOW
            digitalWrite(PIN_STATUS_LED2, led1State ? HIGH : LOW);      // Opposite of LED1
            lastBlinkTime = millis();
        }
    } else if (state == STATE_CALIBRATING) {
        // CALIBRATING: Both LEDs blink to show pass number (1-3 blinks)
        // Pattern: blink ON/OFF for pass count, then pause, repeat
        static unsigned long calibBlinkStartTime = 0;
        static uint8_t calibBlinkCount = 0;
        static bool calibBlinkState = false;
        static bool calibInPause = false;

        unsigned long calibBlinkElapsed = millis() - calibBlinkStartTime;

        if (calibInPause) {
            // Pause between blink sequences (1 second)
            digitalWrite(PIN_STATUS_LED1, HIGH);  // OFF
            digitalWrite(PIN_STATUS_LED2, HIGH);  // OFF
            if (calibBlinkElapsed >= 1000) {
                calibInPause = false;
                calibBlinkCount = 0;
                calibBlinkStartTime = millis();
            }
        } else {
            // Blinking pattern
            if (calibBlinkState) {
                // LED is ON, turn OFF after 150ms
                if (calibBlinkElapsed >= 150) {
                    digitalWrite(PIN_STATUS_LED1, HIGH);  // OFF
                    digitalWrite(PIN_STATUS_LED2, HIGH);  // OFF
                    calibBlinkState = false;
                    calibBlinkCount++;
                    calibBlinkStartTime = millis();

                    // Check if we've completed all blinks for this pass
                    if (calibBlinkCount >= calibrationPass) {
                        calibInPause = true;
                        calibBlinkStartTime = millis();
                    }
                }
            } else {
                // LED is OFF, turn ON after 150ms (if more blinks needed)
                if (calibBlinkElapsed >= 150) {
                    if (calibBlinkCount < calibrationPass) {
                        digitalWrite(PIN_STATUS_LED1, LOW);   // ON
                        digitalWrite(PIN_STATUS_LED2, LOW);   // ON
                        calibBlinkState = true;
                        calibBlinkStartTime = millis();
                    }
                }
            }
        }
    } else {
        // LED Status2: Blink the slot number when blinkingSlotNumber is true
        if (blinkingSlotNumber) {
            if (millis() - lastBlinkTime >= 300) {  // 300ms per blink
                lastBlinkTime = millis();

                if (blinkLedState) {
                    // LED is ON, turn it OFF and decrement counter
                    digitalWrite(PIN_STATUS_LED2, HIGH);  // OFF (active low)
                    blinkLedState = false;
                    blinksRemaining--;

                    // Check if we're done blinking
                    if (blinksRemaining == 0) {
                        blinkingSlotNumber = false;
                    }
                } else {
                    // LED is OFF, turn it ON if we have blinks remaining
                    if (blinksRemaining > 0) {
                        digitalWrite(PIN_STATUS_LED2, LOW);  // ON (active low)
                        blinkLedState = true;
                    }
                }
            }
        } else {
            // LED Status2: OFF when not blinking
            digitalWrite(PIN_STATUS_LED2, HIGH);  // OFF (active low)
        }

        // LED Status1: Blinks during movement, solid when idle
        if (state == STATE_MOVING || state == STATE_HOMING) {
            static unsigned long led1BlinkTime = 0;
            if (millis() - led1BlinkTime > 200) {
                led1State = !led1State;
                digitalWrite(PIN_STATUS_LED1, led1State ? LOW : HIGH);
                led1BlinkTime = millis();
            }
        } else {
            // Solid on when idle
            digitalWrite(PIN_STATUS_LED1, LOW);  // ON (active low)
        }
    }

    // State machine
    switch (state) {
        case STATE_INIT:
            handleStateInit();
            break;
        case STATE_IDLE:
            handleStateIdle();
            break;
        case STATE_MOVING:
            handleStateMoving();
            break;
        case STATE_HOMING:
            handleStateHoming();
            break;
        case STATE_CALIBRATING:
            handleStateCalibrating();
            break;
        case STATE_ERROR:
            handleStateError();
            break;
    }
}

// ===== Public Command Methods =====

FwError FilterWheel::moveToSlot(uint8_t slot) {
    if (state != STATE_IDLE) {
        DEBUG_PRINTLN("ERROR: Not idle");
        return FW_ERR_BUSY;
    }

    if (!calibration.isValid()) {
        DEBUG_PRINTLN("ERROR: No calibration data");
        return FW_ERR_NOT_CALIBRATED;
    }

    if (slot >= calibration.getNumSlots()) {
        DEBUG_PRINTLN("ERROR: Invalid slot number");
        return FW_ERR_BAD_PARAM;
    }

    targetSlot = slot;
    inPulse = false;
    applyingOffset = false;
    pulsesSeen = 0;

    // Start motor immediately (same pattern as goHome)
    stepper.runConstSpeed(STEPPER_SPEED);

    state = STATE_MOVING;
    stateStartTime = millis();

    DEBUG_PRINT("Moving to slot ");
    DEBUG_PRINTLN(targetSlot);

    return FW_OK;
}

FwError FilterWheel::goHome() {
    if (state != STATE_IDLE) {
        DEBUG_PRINTLN("ERROR: Not idle");
        return FW_ERR_BUSY;
    }

    if (!calibration.isValid()) {
        DEBUG_PRINTLN("ERROR: No calibration data");
        return FW_ERR_NOT_CALIBRATED;
    }

    inPulse = false;
    applyingOffset = false;
    pulsesSeen = 0;

    // Start continuous movement at constant speed to find home
    stepper.runConstSpeed(STEPPER_SPEED);

    state = STATE_HOMING;
    stateStartTime = millis();

    DEBUG_PRINTLN("Homing...");

    return FW_OK;
}

FwError FilterWheel::calibrate() {
    if (state != STATE_IDLE) {
        DEBUG_PRINTLN("ERROR: Not idle");
        return FW_ERR_BUSY;
    }

    positionValid = false;
    state = STATE_CALIBRATING;
    stateStartTime = millis();

    DEBUG_PRINTLN("Starting calibration...");

    return FW_OK;
}

FwError FilterWheel::moveSteps(int32_t steps) {
    if (state != STATE_IDLE) {
        DEBUG_PRINTLN("ERROR: Not idle - cannot move steps");
        return FW_ERR_BUSY;
    }

    if (steps == 0) {
        DEBUG_PRINTLN("No movement - steps = 0");
        return FW_OK;
    }

    DEBUG_PRINT("Moving ");
    DEBUG_PRINT(steps);
    DEBUG_PRINTLN(" steps using stepper.move()");

    stepper.move(steps);

    // Switch to step movement mode to track completion
    stepMovementMode = true;
    positionValid = false;  // wheel was moved off a slot
    state = STATE_MOVING;   // Temporarily use MOVING state to block other commands

    DEBUG_PRINT("Step movement initiated, target position: ");
    DEBUG_PRINTLN(stepper.currentPosition() + steps);

    return FW_OK;
}

void FilterWheel::stopMovement() {
    if (state == STATE_MOVING || state == STATE_HOMING ||
        state == STATE_CALIBRATING || state == STATE_ERROR) {
        stepper.stop();
        stepper.setCurrentPosition(0);
        stepMovementMode = false;
        applyingOffset = false;
        positionValid = false;  // halted between slots
        state = STATE_IDLE;
        DEBUG_PRINTLN("Movement stopped");
    }
}

// ===== Status / error helpers =====

int16_t FilterWheel::getReportedSlot() const {
    if (state == STATE_IDLE || state == STATE_ERROR) {
        return currentSlot;
    }
    return -1;  // INIT, MOVING, HOMING, CALIBRATING
}

int16_t FilterWheel::getTargetSlot() const {
    if (state == STATE_MOVING && !stepMovementMode) {
        return targetSlot;
    }
    return -1;
}

uint8_t FilterWheel::getFlags() const {
    uint8_t flags = 0;
    if (calibration.isValid()) flags |= 0x01;
    if (positionValid)         flags |= 0x02;
    if (buttonPressedDuringBoot) flags |= 0x04;
    if (eepromFault)           flags |= 0x08;
    return flags;
}

bool FilterWheel::checkDriver() {
    lastDriverCheck = millis();
    if (driver.test_connection() != 0) {
        DEBUG_PRINTLN("ERROR: TMC2209 not answering");
        return false;
    }
    if (driver.DRV_STATUS() & TMC_FAULT_MASK) {
        DEBUG_PRINTLN("ERROR: TMC2209 reports overtemp/short");
        return false;
    }
    return true;
}

// Pick the most specific cause for a move/home/calibration that ran out of time
FwError FilterWheel::classifyTimeout(FwError timeoutCode) {
    if (!checkDriver()) return FW_ERR_MOTOR_DRIVER;
    if (pulsesSeen == 0) return FW_ERR_HALL_NOT_SEEN;
    return timeoutCode;
}

// Sticky fault: remember the cause, stop the motor, enter STATE_ERROR
void FilterWheel::raiseError(FwError code) {
    lastError = code;
    stepper.emergencyStop();
    stepMovementMode = false;
    applyingOffset = false;
    positionValid = false;
    state = STATE_ERROR;
    stateStartTime = millis();  // ERROR state lasts ERROR_RECOVER_MS from now
    DEBUG_PRINT("ERROR code ");
    DEBUG_PRINTLN((int)code);
}

// ===== State Handlers =====

void FilterWheel::handleStateInit() {
    // Try to load calibration data from EEPROM
    if (calibration.load() && calibration.isValid()) {
        DEBUG_PRINTLN("Valid calibration found in EEPROM");
        calibration.printCalibration();

        // Set current slot to home (assume we're at home on startup)
        currentSlot = calibration.findHomeSlot();
        DEBUG_PRINT("Assumed position at startup: slot ");
        DEBUG_PRINTLN(currentSlot);

        // Check if button was pressed during boot - force calibration
        if (buttonPressedDuringBoot) {
            DEBUG_PRINTLN("Button held during boot - forcing calibration");
            stateStartTime = millis();

            state = STATE_CALIBRATING;
            buttonPressedDuringBoot = false;  // Clear flag
        } else {
            // Execute startup behavior based on EEPROM setting
            uint8_t startupBehavior = calibration.getStartupBehavior();

            if (startupBehavior == 1) {
                // Home on startup
                DEBUG_PRINTLN("Startup behavior: HOMING");
                stateStartTime = millis();

                // Start continuous movement at constant speed to find home
                pulsesSeen = 0;
                stepper.runConstSpeed(STEPPER_SPEED);

                state = STATE_HOMING;
            } else {
                // Stay at current position (default)
                DEBUG_PRINTLN("Startup behavior: STAY at current position");
                state = STATE_IDLE;
            }
        }


    } else {
        DEBUG_PRINTLN("No valid calibration found - starting auto-calibration");

        // Automatically start calibration
        stateStartTime = millis();

        state = STATE_CALIBRATING;
    }
}

void FilterWheel::handleStateIdle() {
    // Commands are received via moveToSlot(), goHome(), calibrate().
    // While idle (motor quiet, UART call cannot disturb hall timing) watch the TMC2209.
    if (millis() - lastDriverCheck > DRIVER_CHECK_MS) {
        if (!checkDriver() && !checkDriver()) {  // two failed reads in a row = real fault
            raiseError(FW_ERR_MOTOR_DRIVER);
        } else if (lastError == FW_ERR_MOTOR_DRIVER) {
            lastError = FW_OK;  // driver recovered
        }
    }
}

void FilterWheel::handleStateMoving() {
    // Handle step movement mode (direct step movements via moveSteps command)
    if (stepMovementMode) {
        if (!stepper.isRunning()) {
            // Step movement complete
            stepMovementMode = false;
            state = STATE_IDLE;
            DEBUG_PRINT("Step movement complete, final position: ");
            DEBUG_PRINTLN(stepper.currentPosition());
        }
        return;  // Don't process hall sensor in step movement mode
    }

    // Start movement on first entry to this state (if not already running)
    if (!applyingOffset && !stepper.isRunning()) {
        stepper.runConstSpeed(STEPPER_SPEED);
    }

    // If we're applying offset (deceleration + offset combined), wait for timer to complete movement
    if (applyingOffset) {
        if (stepper.isRunning()) {
            return;  // Continue deceleration + offset movement
        } else {
            // Movement complete
            applyingOffset = false;

            currentSlot = targetSlot;
            positionValid = true;

            state = STATE_IDLE;
            DEBUG_PRINT("Arrived at slot ");
            DEBUG_PRINTLN(currentSlot);

            // Start blinking the slot number on LED Status2
            blinkingSlotNumber = true;
            blinksRemaining = currentSlot + 1;  // Slot 0 = 1 blink, Slot 1 = 2 blinks, etc.
            lastBlinkTime = millis();
            blinkLedState = false;  // Start with LED OFF
            digitalWrite(PIN_STATUS_LED2, HIGH);  // Ensure LED is OFF to start

            return;
        }
    }

    // Timer interrupt handles constant speed movement
    // Just monitor hall sensor for magnet detection

    // Check if we've detected a magnet
    bool hallActive = hallSensor.isMagnetPresent();

    if (hallActive && !inPulse) {
        // Start of pulse
        inPulse = true;
        pulseStartPosition = stepper.currentPosition();
        DEBUG_PRINTLN("Moving: Pulse start detected");
    }
    else if (!hallActive && inPulse) {
        // End of pulse - check if this matches target slot
        inPulse = false;
        pulsesSeen++;
        uint16_t pulseLength = stepper.currentPosition() - pulseStartPosition;

        DEBUG_PRINT("Moving: Pulse detected, length = ");
        DEBUG_PRINT(pulseLength);
        DEBUG_PRINTLN(" steps");

        uint8_t detectedSlot;
        if (detectSlotByPulse(pulseLength, detectedSlot)) {
            DEBUG_PRINT("Moving: Identified as slot ");
            DEBUG_PRINT(detectedSlot);
            DEBUG_PRINT(" (target: ");
            DEBUG_PRINT(targetSlot);
            DEBUG_PRINTLN(")");

            if (detectedSlot == targetSlot) {
                DEBUG_PRINTLN("Moving: Target slot found!");

                // Calculate offset and immediately switch to deceleration + offset movement
                int16_t offset = calibration.getTotalOffset(targetSlot);

                DEBUG_PRINT("Moving: Target slot = ");
                DEBUG_PRINTLN(targetSlot);
                DEBUG_PRINT("Moving: Base offset = ");
                DEBUG_PRINTLN(calibration.getBaseOffset());
                DEBUG_PRINT("Moving: User offset[");
                DEBUG_PRINT(targetSlot);
                DEBUG_PRINT("] = ");
                DEBUG_PRINTLN(calibration.getUserOffset(targetSlot));
                DEBUG_PRINT("Moving: Total offset = ");
                DEBUG_PRINT(offset);
                DEBUG_PRINTLN(" steps");

                if (offset == 0) {
                    // No offset - just decelerate to stop
                    DEBUG_PRINTLN("Moving: Zero offset - decelerating to stop");
                    stepper.stop(true);
                    applyingOffset = true;
                } else {
                    // move() will naturally decelerate from current speed and apply offset in one movement
                    stepper.move(offset);
                    applyingOffset = true;
                }
            } else {
                DEBUG_PRINTLN("Moving: Not target slot, continuing...");
            }
        } else {
            DEBUG_PRINTLN("Moving: Pulse not matched to any slot");
        }
    }

    // Timeout check (30 seconds)
    if (millis() - stateStartTime > MOVE_TIMEOUT_MS) {
        DEBUG_PRINTLN("ERROR: Move timeout");
        raiseError(classifyTimeout(FW_ERR_MOVE_TIMEOUT));
    }
}

void FilterWheel::handleStateHoming() {
    // If we're applying offset (deceleration + offset combined), wait for timer to complete movement
    if (applyingOffset) {
        if (stepper.isRunning()) {
            return;  // Continue deceleration + offset movement
        } else {
            // Movement complete
            applyingOffset = false;
            currentSlot = calibration.findHomeSlot();
            positionValid = true;

            state = STATE_IDLE;
            DEBUG_PRINTLN("Homed successfully");

            // Start blinking the slot number on LED Status2
            blinkingSlotNumber = true;
            blinksRemaining = currentSlot + 1;  // Slot 0 = 1 blink, Slot 1 = 2 blinks, etc.
            lastBlinkTime = millis();
            blinkLedState = false;  // Start with LED OFF
            digitalWrite(PIN_STATUS_LED2, HIGH);  // Ensure LED is OFF to start

            return;
        }
    }

    // Timer interrupt handles constant speed movement
    // Just monitor hall sensor for magnet detection

    // Check for hall sensor pulses
    bool hallActive = hallSensor.isMagnetPresent();

    if (hallActive && !inPulse) {
        inPulse = true;
        pulseStartPosition = stepper.currentPosition();
        DEBUG_PRINTLN("Homing: Pulse start detected");
    }
    else if (!hallActive && inPulse) {
        inPulse = false;
        pulsesSeen++;
        uint16_t pulseLength = stepper.currentPosition() - pulseStartPosition;

        DEBUG_PRINT("Homing: Pulse detected, length = ");
        DEBUG_PRINT(pulseLength);
        DEBUG_PRINTLN(" steps");

        // Check if this is the home slot (shortest pulse)
        uint8_t homeSlot = calibration.findHomeSlot();
        uint16_t homePulseLength = calibration.getPulseLength(homeSlot);
        uint16_t tolerance = homePulseLength / 20;  // 5% tolerance

        DEBUG_PRINT("Homing: Expected home pulse = ");
        DEBUG_PRINT(homePulseLength);
        DEBUG_PRINT(" steps, tolerance = +/- ");
        DEBUG_PRINT(tolerance);
        DEBUG_PRINTLN(" steps");

        if (pulseLength >= (homePulseLength - tolerance) &&
            pulseLength <= (homePulseLength + tolerance)) {

            DEBUG_PRINTLN("Homing: Home slot match found!");
            currentSlot = homeSlot;  // Update current slot

            // Calculate offset and immediately switch to deceleration + offset movement
            int16_t offset = calibration.getTotalOffset(homeSlot);

            DEBUG_PRINT("Homing: Home slot = ");
            DEBUG_PRINTLN(homeSlot);
            DEBUG_PRINT("Homing: Base offset = ");
            DEBUG_PRINTLN(calibration.getBaseOffset());
            DEBUG_PRINT("Homing: User offset[");
            DEBUG_PRINT(homeSlot);
            DEBUG_PRINT("] = ");
            DEBUG_PRINTLN(calibration.getUserOffset(homeSlot));
            DEBUG_PRINT("Homing: Total offset = ");
            DEBUG_PRINT(offset);
            DEBUG_PRINTLN(" steps");

            if (offset == 0) {
                // No offset - just decelerate to stop
                DEBUG_PRINTLN("Homing: Zero offset - decelerating to stop");
                stepper.stop(true);
                applyingOffset = true;
            } else {
                // move() will naturally decelerate from current speed and apply offset in one movement
                stepper.move(offset);
                applyingOffset = true;
            }
        } else {
            DEBUG_PRINTLN("Homing: Not home slot, continuing search...");
        }
    }

    // Timeout check (30 seconds)
    if (millis() - stateStartTime > HOME_TIMEOUT_MS) {
        DEBUG_PRINTLN("ERROR: Homing timeout");
        raiseError(classifyTimeout(FW_ERR_HOME_TIMEOUT));
    }
}

void FilterWheel::handleStateCalibrating() {
    static uint32_t pulseSums[MAX_SLOTS] = {0};      // Sum of samples for averaging
    static uint8_t sampleCounts[MAX_SLOTS] = {0};    // Number of samples per slot
    static uint16_t firstSamples[MAX_SLOTS] = {0};   // First sample of each slot
    static uint8_t slotCount = 0;
    static bool skipFirstPulse = true;
    static bool calibrationComplete = false;
    static const uint8_t SAMPLES_PER_SLOT = 3;
    static int32_t firstRotationDistance = 0;        // Distance for one complete rotation
    static bool firstRotationComplete = false;

    // Reset static variables on entry
    if (millis() - stateStartTime < 100) {
        memset(pulseSums, 0, sizeof(pulseSums));
        memset(sampleCounts, 0, sizeof(sampleCounts));
        memset(firstSamples, 0, sizeof(firstSamples));
        slotCount = 0;
        skipFirstPulse = true;
        calibrationComplete = false;
        calibrationPass = 0;
        firstRotationDistance = 0;
        firstRotationComplete = false;
        pulsesSeen = 0;
        positionValid = false;
        DEBUG_PRINTLN("=== CALIBRATION STARTED ===");
        DEBUG_PRINT("Motor enabled, speed = ");
        DEBUG_PRINTLN(STEPPER_SPEED);
    }

    // Calculate current calibration pass (1-3) based on minimum samples across all slots
    if (slotCount > 0) {
        uint8_t minSamples = SAMPLES_PER_SLOT;
        for (uint8_t i = 0; i < slotCount; i++) {
            if (sampleCounts[i] < minSamples) {
                minSamples = sampleCounts[i];
            }
        }
        calibrationPass = minSamples + 1;  // Pass 1, 2, or 3
        if (calibrationPass > SAMPLES_PER_SLOT) {
            calibrationPass = SAMPLES_PER_SLOT;
        }
    } else {
        calibrationPass = 1;  // First pass before any slots detected
    }

    if (calibrationComplete) {
        // Calculate averages and save to EEPROM
        calibration.clear();
        calibration.setNumSlots(slotCount);

        // Calculate max steps based on actual first rotation distance
        // firstRotationDistance was captured when we returned to slot 0 for the second time
        // This represents one complete rotation through all slots
        uint32_t maxSteps;

        if (firstRotationComplete && firstRotationDistance > 0) {
            // Apply 1.1x safety margin to first rotation distance
            maxSteps = (uint32_t)(firstRotationDistance * 1.1);

            DEBUG_PRINT("Calibration: First rotation distance = ");
            DEBUG_PRINT(firstRotationDistance);
            DEBUG_PRINTLN(" steps");
            DEBUG_PRINT("Calibration: Max steps (1.1x safety) = ");
            DEBUG_PRINTLN(maxSteps);
        } else {
            // Fallback: use hardcoded value if we didn't capture first rotation
            maxSteps = MAXSTEPS;
            DEBUG_PRINTLN("WARNING: First rotation not captured, using default MAXSTEPS");
        }

        calibration.setMaxSteps(maxSteps);

        // Calculate average pulse lengths
        uint16_t avgPulseLengths[MAX_SLOTS];
        for (uint8_t i = 0; i < slotCount; i++) {
            avgPulseLengths[i] = pulseSums[i] / sampleCounts[i];
            DEBUG_PRINT("Slot ");
            DEBUG_PRINT(i);
            DEBUG_PRINT(": ");
            DEBUG_PRINT(sampleCounts[i]);
            DEBUG_PRINT(" samples, avg pulse = ");
            DEBUG_PRINTLN(avgPulseLengths[i]);
        }

        // Sort averaged pulse lengths
        for (uint8_t i = 0; i < slotCount - 1; i++) {
            for (uint8_t j = i + 1; j < slotCount; j++) {
                if (avgPulseLengths[j] < avgPulseLengths[i]) {
                    uint16_t temp = avgPulseLengths[i];
                    avgPulseLengths[i] = avgPulseLengths[j];
                    avgPulseLengths[j] = temp;
                }
            }
        }

        // Store sorted averages
        for (uint8_t i = 0; i < slotCount; i++) {
            calibration.setPulseLength(i, avgPulseLengths[i]);
            calibration.setOffset(i, BASE_OFFSET);  // Fixed offset for testing
        }

        if (calibration.save()) {
            DEBUG_PRINT("Calibration complete - detected ");
            DEBUG_PRINT(slotCount);
            DEBUG_PRINTLN(" slots");
            calibration.printCalibration();

            // Now go to home position
            DEBUG_PRINTLN("Finding home position...");
            stateStartTime = millis();
            pulsesSeen = 0;
            state = STATE_HOMING;
            // Keep driver enabled (already enabled during calibration)
        } else {
            DEBUG_PRINTLN("ERROR: Failed to save calibration");
            raiseError(FW_ERR_EEPROM);
        }

        return;
    }

    // Start stepper if not already running
    if (!stepper.isRunning()) {
        stepper.runConstSpeed(STEPPER_SPEED);
    }

    // Detect pulses
    bool hallActive = hallSensor.isMagnetPresent();

    if (hallActive && !inPulse) {
        inPulse = true;
        pulseStartPosition = stepper.currentPosition();
    }
    else if (!hallActive && inPulse) {
        inPulse = false;
        pulsesSeen++;

        if (skipFirstPulse) {
            skipFirstPulse = false;
            DEBUG_PRINTLN("Skipping first pulse");
        } else {
            uint16_t pulseLength = stepper.currentPosition() - pulseStartPosition;

            // Match pulse to existing slot (using first sample for tolerance)
            bool foundMatch = false;
            for (uint8_t i = 0; i < slotCount; i++) {
                uint16_t tolerance = firstSamples[i] / 20;  // 5% tolerance
                if (pulseLength >= (firstSamples[i] - tolerance) &&
                    pulseLength <= (firstSamples[i] + tolerance)) {
                    // Found matching slot - add sample
                    pulseSums[i] += pulseLength;
                    sampleCounts[i]++;
                    foundMatch = true;

                    DEBUG_PRINT("Slot ");
                    DEBUG_PRINT(i);
                    DEBUG_PRINT(" sample #");
                    DEBUG_PRINT(sampleCounts[i]);
                    DEBUG_PRINT(": ");
                    DEBUG_PRINTLN(pulseLength);

                    // Capture first rotation distance when we return to slot 0 for the second time
                    // This means we've completed one full rotation (detected all slots once and returned to slot 0)
                    if (!firstRotationComplete && i == 0 && sampleCounts[i] == 2) {
                        firstRotationDistance = stepper.currentPosition();
                        firstRotationComplete = true;
                        DEBUG_PRINT("First rotation complete! Distance = ");
                        DEBUG_PRINT(firstRotationDistance);
                        DEBUG_PRINTLN(" steps");
                    }

                    // Check if all slots have enough samples
                    bool allSlotsComplete = true;
                    for (uint8_t j = 0; j < slotCount; j++) {
                        if (sampleCounts[j] < SAMPLES_PER_SLOT) {
                            allSlotsComplete = false;
                            break;
                        }
                    }

                    if (allSlotsComplete) {
                        calibrationComplete = true;
                        DEBUG_PRINTLN("All slots have 3 samples - calibration complete");
                    }
                    break;
                }
            }

            // New slot detected
            if (!foundMatch && slotCount < MAX_SLOTS) {
                firstSamples[slotCount] = pulseLength;
                pulseSums[slotCount] = pulseLength;
                sampleCounts[slotCount] = 1;
                DEBUG_PRINT("New slot ");
                DEBUG_PRINT(slotCount);
                DEBUG_PRINT(" detected, pulse = ");
                DEBUG_PRINTLN(pulseLength);
                slotCount++;
            }
        }
    }

    // Periodic status update (every 10 seconds)
    static unsigned long lastStatusPrint = 0;
    if (millis() - lastStatusPrint > 10000) {
        lastStatusPrint = millis();
        DEBUG_PRINT("Calibration status: ");
        DEBUG_PRINT(slotCount);
        DEBUG_PRINT(" slots detected, samples: [");
        for (uint8_t i = 0; i < slotCount; i++) {
            if (i > 0) DEBUG_PRINT(", ");
            DEBUG_PRINT(sampleCounts[i]);
        }
        DEBUG_PRINT("], position: ");
        DEBUG_PRINTLN(stepper.currentPosition());
    }

    // Timeout check (90 seconds - need more time for 3 rotations)
    if (millis() - stateStartTime > CALIBRATE_TIMEOUT_MS) {
        DEBUG_PRINTLN("ERROR: Calibration timeout");
        DEBUG_PRINT("Final status: ");
        DEBUG_PRINT(slotCount);
        DEBUG_PRINT(" slots, samples: [");
        for (uint8_t i = 0; i < slotCount; i++) {
            if (i > 0) DEBUG_PRINT(", ");
            DEBUG_PRINT(sampleCounts[i]);
        }
        DEBUG_PRINTLN("]");
        raiseError(classifyTimeout(FW_ERR_CALIB_FAILED));
    }
}

void FilterWheel::handleStateError() {
    if (stepper.isRunning()) {
        stepper.emergencyStop();
        DEBUG_PRINTLN("ERROR: Motor stopped");
    }

    // Auto-recover to IDLE after 10 s so a reconnect attempt succeeds
    // without requiring a power cycle (e.g. after accidental command from wrong port).
    if (millis() - stateStartTime > ERROR_RECOVER_MS) {
        DEBUG_PRINTLN("ERROR: Auto-recovering to IDLE");
        stateStartTime = millis();
        state = STATE_IDLE;
    }
}

// ===== Helper Methods =====

uint8_t FilterWheel::findHomeSlot() {
    return calibration.findHomeSlot();
}

bool FilterWheel::detectSlotByPulse(uint16_t pulseLength, uint8_t& slotNumber) {
    if (!calibration.isValid()) {
        return false;
    }

    for (uint8_t i = 0; i < calibration.getNumSlots(); i++) {
        uint16_t storedPulse = calibration.getPulseLength(i);
        uint16_t tolerance = storedPulse / 20;  // 5% tolerance - matches homing

        if (pulseLength >= (storedPulse - tolerance) &&
            pulseLength <= (storedPulse + tolerance)) {
            slotNumber = i;
            return true;
        }
    }

    return false;
}

void FilterWheel::handleButtonEvents() {
    // Handle long press (calibration) in both IDLE and ERROR states
    if (button.wasLongPress()) {
        if (state == STATE_IDLE || state == STATE_ERROR) {
            DEBUG_PRINTLN("Button: Long press - starting calibration");
            state = STATE_CALIBRATING;
            stateStartTime = millis();

        }
        return;  // Don't process other button events
    }

    // Only handle other button events when IDLE
    if (state != STATE_IDLE) {
        return;
    }

    // CW Button - Single click: Move to next slot (clockwise)
    if (button.wasSingleClick()) {
        DEBUG_PRINTLN("Button CW: Single click - moving to next slot");

        if (!calibration.isValid()) {
            DEBUG_PRINTLN("Button: No calibration, cannot move");
            return;
        }

        uint8_t nextSlot = (currentSlot + 1) % calibration.getNumSlots();
        moveToSlot(nextSlot);
    }

    // CCW Button - Single click: Move to previous slot (counter-clockwise)
    if (buttonCCW.wasSingleClick()) {
        DEBUG_PRINTLN("Button CCW: Single click - moving to previous slot");

        if (!calibration.isValid()) {
            DEBUG_PRINTLN("Button: No calibration, cannot move");
            return;
        }

        uint8_t numSlots = calibration.getNumSlots();
        uint8_t prevSlot = (currentSlot == 0) ? (numSlots - 1) : (currentSlot - 1);
        moveToSlot(prevSlot);
    }

    // CW Button - Double click: Find home
    if (button.wasDoubleClick()) {
        DEBUG_PRINTLN("Button CW: Double click - finding home");

        if (!calibration.isValid()) {
            DEBUG_PRINTLN("Button: No calibration, cannot home");
            return;
        }

        goHome();
    }

    // CCW Button - Double click: Find home (same as CW button)
    if (buttonCCW.wasDoubleClick()) {
        DEBUG_PRINTLN("Button CCW: Double click - finding home");

        if (!calibration.isValid()) {
            DEBUG_PRINTLN("Button: No calibration, cannot home");
            return;
        }

        goHome();
    }
}

// Reboot into DFU bootloader mode
// This directly jumps to the STM32 system bootloader
void FilterWheel::rebootToDFU() {
    DEBUG_PRINTLN("Jumping to DFU bootloader...");

    // Small delay to ensure serial transmission completes
    delay(100);

    // Jump directly to STM32 system bootloader
    // This function performs all necessary cleanup and jumps to bootloader
    JumpToBootloader();
}
