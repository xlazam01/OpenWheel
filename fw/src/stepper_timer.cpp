// Timer-based stepper motor driver implementation
#include "stepper_timer.h"

#include "config.h"

#include <HardwareTimer.h>

// Timer configuration
#define TIMER_FREQ 10000       // 10kHz base timer frequency for oversampling

// Global instance
StepperTimer stepper;

// Hardware timer instance (TIM3)
HardwareTimer *stepTimer = nullptr;

// Timer callback function
void timerCallback(void) {
    stepper.handleStepInterrupt();
}

StepperTimer::StepperTimer() :
    maxSpeed(STEPPER_MAX_SPEED),
    acceleration(STEPPER_ACCEL),
    state(STEPPER_STATE_IDLE),
    currentPos(0),
    targetPos(0),
    currentSpeed(0),
    targetSpeed(0),
    timerPeriod(0),
    stepCounter(0),
    ticksPerStep(0),
    tickCounter(0),
    direction(1),
    accelSteps(0),
    decelSteps(0),
    constSteps(0),
    continuousMode(false),
    lastUpdateTime(0)
{
}

void StepperTimer::begin() {
    // Configure DIR and STEP pins
    pinMode(DIR_PIN, OUTPUT);
    pinMode(STEP_PIN, OUTPUT);
    digitalWrite(DIR_PIN, LOW);
    digitalWrite(STEP_PIN, LOW);

    // Initialize HardwareTimer
    stepTimer = new HardwareTimer(TIM3);  // STM32G0B1 has no TIM4
    DEBUG_PRINTLN("StepperTimer: Using TIM3");

    // Set timer to 10kHz base frequency for oversampling
    stepTimer->setOverflow(TIMER_FREQ, HERTZ_FORMAT);

    // Attach callback function
    stepTimer->attachInterrupt(timerCallback);

    // Set highest interrupt priority (0)
    stepTimer->setInterruptPriority(0, 0);

    // Timer will be started when move() or runConstSpeed() is called

    DEBUG_PRINTLN("StepperTimer: Initialized at 10kHz with oversampling");
}

uint16_t StepperTimer::speedToTimerPeriod(uint16_t speed) {
    // With oversampling at 10kHz, calculate ticks per step
    // ticksPerStep = TIMER_FREQ / speed
    // Example: 1000 steps/sec = 10000/1000 = 10 ticks per step
    // Example: 100 steps/sec = 10000/100 = 100 ticks per step
    if (speed == 0) return 10000;  // Maximum ticks (slowest speed)
    uint16_t ticks = TIMER_FREQ / speed;
    if (ticks < 1) ticks = 1;  // Minimum 1 tick (fastest speed)
    return ticks;
}

void StepperTimer::move(int32_t steps) {
    if (steps == 0) return;

    // Stop any current movement
    if (stepTimer) stepTimer->pause();

    // Set direction
    direction = (steps > 0) ? 1 : -1;
    digitalWrite(DIR_PIN, (direction > 0) ? HIGH : LOW);

    // Calculate target position
    targetPos = currentPos + steps;
    uint32_t totalSteps = abs(steps);

    // Calculate trapezoidal profile
    // v^2 = 2*a*s  =>  s = v^2 / (2*a)
    uint32_t accelDistance = ((uint32_t)maxSpeed * maxSpeed) / (2 * acceleration);
    uint32_t decelDistance = accelDistance;  // Symmetric profile

    if (totalSteps <= (accelDistance + decelDistance)) {
        // Short move - triangular profile (no constant speed phase)
        // Peak speed = sqrt(a * totalSteps)
        uint32_t peakSpeed = sqrt((uint32_t)acceleration * totalSteps);
        if (peakSpeed > maxSpeed) peakSpeed = maxSpeed;

        accelSteps = totalSteps / 2;
        decelSteps = totalSteps - accelSteps;
        constSteps = 0;
        targetSpeed = peakSpeed;
    } else {
        // Long move - full trapezoidal profile
        accelSteps = accelDistance;
        decelSteps = decelDistance;
        constSteps = totalSteps - accelSteps - decelSteps;
        targetSpeed = maxSpeed;
    }

    // Initialize movement
    currentSpeed = 100;  // Start at minimum speed to avoid resonance
    stepCounter = totalSteps;
    state = STEPPER_STATE_ACCEL;
    continuousMode = false;
    lastUpdateTime = 0;  // Will be initialized on first update() call

    // Initialize oversampling
    ticksPerStep = speedToTimerPeriod(100);  // Start at 100 steps/sec
    tickCounter = 0;

    // Start timer (already running at constant 10kHz)
    stepTimer->resume();

    // Movement initialized
}

void StepperTimer::runConstSpeed(int16_t speed) {
    if (speed == 0) {
        stop();
        return;
    }

    // Set direction based on sign of speed
    direction = (speed > 0) ? 1 : -1;
    digitalWrite(DIR_PIN, (direction > 0) ? HIGH : LOW);

    // Use absolute value for speed
    uint16_t absSpeed = abs(speed);
    if (absSpeed > maxSpeed) absSpeed = maxSpeed;

    // Stop current movement
    if (stepTimer) stepTimer->pause();

    // Setup continuous movement with acceleration
    // Start from minimum speed to avoid resonance (100 steps/sec)
    const uint16_t minStartSpeed = 100;  // Minimum starting speed to avoid resonance

    if (state == STEPPER_STATE_IDLE) {
        currentSpeed = minStartSpeed;  // Start at minimum speed to avoid resonance
    }
    // else keep current speed for smooth transition

    targetSpeed = absSpeed;
    continuousMode = true;
    stepCounter = 0;  // Not used in continuous mode

    // If already at target speed, go directly to constant speed state
    if (currentSpeed >= targetSpeed) {
        state = STEPPER_STATE_CONST;
        currentSpeed = targetSpeed;
    } else {
        // Start in acceleration state to ramp up
        state = STEPPER_STATE_ACCEL;
        // Store starting position for acceleration tracking (reuse accelSteps variable)
        accelSteps = currentPos;
    }

    // Initialize oversampling
    ticksPerStep = speedToTimerPeriod(currentSpeed);
    tickCounter = 0;

    // Start timer (already running at constant 10kHz)
    stepTimer->resume();

    // Continuous movement initialized
}

void StepperTimer::stop(bool decelerate) {
    if (state == STEPPER_STATE_IDLE) return;

    if (decelerate) {
        // Smooth stop with deceleration
        if (continuousMode) {
            state = STEPPER_STATE_STOPPING;

            // Calculate deceleration distance from current speed
            // v^2 = 2*a*s  =>  s = v^2 / (2*a)
            uint32_t decelDistance = ((uint32_t)currentSpeed * currentSpeed) / (2 * acceleration);
            if (decelDistance < 1) decelDistance = 1;

            DEBUG_PRINT("STOP: currentSpeed=");
            DEBUG_PRINT(currentSpeed);
            DEBUG_PRINT(" steps/sec, decelDistance=");
            DEBUG_PRINT(decelDistance);
            DEBUG_PRINTLN(" steps");

            // Store current position and decel distance
            accelSteps = currentPos;  // Starting position for deceleration
            this->decelSteps = decelDistance;  // Steps needed to decelerate

            // IMPORTANT: Keep continuousMode true during deceleration so ISR continues generating pulses
            // It will be cleared when we reach IDLE state
        } else {
            // Already in move mode, just force deceleration
            state = STEPPER_STATE_DECEL;
            decelSteps = stepCounter;  // Use all remaining steps for decel
            accelSteps = 0;
            constSteps = 0;
        }
    } else {
        // Instant stop - same as emergencyStop
        if (stepTimer) stepTimer->pause();
        state = STEPPER_STATE_IDLE;
        currentSpeed = 0;
        stepCounter = 0;
        continuousMode = false;
        targetPos = currentPos;
    }
}

void StepperTimer::emergencyStop() {
    if (stepTimer) stepTimer->pause();  // Disable timer immediately
    state = STEPPER_STATE_IDLE;
    currentSpeed = 0;
    stepCounter = 0;
    continuousMode = false;
    targetPos = currentPos;  // Update target to current position

    DEBUG_PRINTLN("StepperTimer: emergencyStop()");
}

void StepperTimer::setMaxSpeed(uint16_t stepsPerSec) {
    maxSpeed = stepsPerSec;
}

void StepperTimer::setAcceleration(uint16_t stepsPerSecSq) {
    acceleration = stepsPerSecSq;
}

int32_t StepperTimer::distanceToGo() const {
    return targetPos - currentPos;
}

int32_t StepperTimer::currentPosition() const {
    return currentPos;
}

bool StepperTimer::isRunning() const {
    return state != STEPPER_STATE_IDLE;
}

StepperState StepperTimer::getState() const {
    return state;
}

void StepperTimer::setCurrentPosition(int32_t pos) {
    noInterrupts();
    currentPos = pos;
    targetPos = pos;
    interrupts();
}

void StepperTimer::update() {
    // This is called from main loop to update acceleration/deceleration
    // The timer ISR handles pulse generation

    if (state == STEPPER_STATE_IDLE) return;

    // Calculate steps completed in current phase (for move mode)
    int32_t totalSteps = abs(targetPos - currentPos + (stepCounter * direction));
    int32_t stepsCompleted = totalSteps - stepCounter;

    // Update speed based on profile
    uint16_t newSpeed = currentSpeed;
    const uint16_t minStartSpeed = 100;  // Minimum starting speed to avoid resonance

    switch (state) {
        case STEPPER_STATE_ACCEL:
            // Use quadratic acceleration profile: v^2 = v0^2 + 2*a*s
            // Where v0 is the initial speed (100 steps/sec to avoid resonance)

            if (continuousMode) {
                // Calculate steps from start of acceleration
                int32_t accelStepsCompleted = abs(currentPos - (int32_t)accelSteps);  // accelSteps stores start position for continuous mode

                // v^2 = v0^2 + 2*a*s
                uint32_t vSquared = (uint32_t)minStartSpeed * minStartSpeed +
                                    2 * (uint32_t)acceleration * accelStepsCompleted;
                newSpeed = sqrt(vSquared);

                if (newSpeed >= targetSpeed) {
                    newSpeed = targetSpeed;
                    state = STEPPER_STATE_CONST;
                } else {
                    if (newSpeed < minStartSpeed) newSpeed = minStartSpeed;  // Never go below minimum
                }
            } else {
                // Move mode: use step-based acceleration with initial speed
                if (stepsCompleted >= accelSteps) {
                    // Transition to constant speed
                    newSpeed = targetSpeed;
                    state = (constSteps > 0) ? STEPPER_STATE_CONST : STEPPER_STATE_DECEL;
                } else {
                    // v^2 = v0^2 + 2*a*s
                    uint32_t vSquared = (uint32_t)minStartSpeed * minStartSpeed +
                                        2 * (uint32_t)acceleration * stepsCompleted;
                    newSpeed = sqrt(vSquared);
                    if (newSpeed > targetSpeed) newSpeed = targetSpeed;
                    if (newSpeed < minStartSpeed) newSpeed = minStartSpeed;  // Never go below minimum
                }
            }
            break;

        case STEPPER_STATE_CONST:
            // Maintain constant speed
            newSpeed = targetSpeed;

            // Check if we need to start decelerating (only in move mode)
            if (!continuousMode && stepCounter <= decelSteps) {
                state = STEPPER_STATE_DECEL;
            }
            break;

        case STEPPER_STATE_DECEL:
            // Decelerate based on remaining steps
            // Decelerate to minimum speed, then stop
            {
                if (stepCounter > 0) {
                    // v^2 = v0^2 + 2*a*s, solving for final speed when decelerating
                    // We want to reach minStartSpeed at the end
                    uint32_t vSquared = (uint32_t)minStartSpeed * minStartSpeed +
                                        2 * (uint32_t)acceleration * stepCounter;
                    newSpeed = sqrt(vSquared);
                    if (newSpeed > targetSpeed) newSpeed = targetSpeed;
                    if (newSpeed < minStartSpeed) newSpeed = minStartSpeed;
                } else {
                    // Reached end, stop timer
                    newSpeed = 0;
                    state = STEPPER_STATE_IDLE;
                    if (stepTimer) stepTimer->pause();
                }
            }
            break;

        case STEPPER_STATE_STOPPING:
            // Decelerate to stop from continuous mode using quadratic profile
            // Calculate steps remaining until stop
            // When stopping is initiated, decelSteps stores the number of steps needed to decelerate from current speed
            {
                int32_t stepsFromStart = abs(currentPos - (int32_t)accelSteps);  // accelSteps stores position when stopping started
                int32_t stepsRemaining = decelSteps - stepsFromStart;

                DEBUG_PRINT("STOPPING: stepsFromStart=");
                DEBUG_PRINT(stepsFromStart);
                DEBUG_PRINT(", stepsRemaining=");
                DEBUG_PRINT(stepsRemaining);
                DEBUG_PRINT(", currentSpeed=");
                DEBUG_PRINTLN(currentSpeed);

                if (stepsRemaining <= 0 || stepsFromStart >= decelSteps) {
                    // Deceleration complete
                    newSpeed = 0;
                    state = STEPPER_STATE_IDLE;
                    continuousMode = false;  // Clear continuous mode flag
                    if (stepTimer) stepTimer->pause();
                    DEBUG_PRINTLN("STOPPING: deceleration complete");
                } else {
                    // Decelerate from current speed to minimum speed over remaining steps
                    // v^2 = v0^2 - 2*a*s (note: subtracting because we're decelerating)
                    // But we calculate based on remaining distance: v = sqrt(2*a*stepsRemaining)
                    // This ensures we reach zero speed exactly when stepsRemaining reaches zero
                    uint32_t vSquared = 2 * (uint32_t)acceleration * stepsRemaining;
                    newSpeed = sqrt(vSquared);

                    DEBUG_PRINT("  -> calculated newSpeed=");
                    DEBUG_PRINTLN(newSpeed);

                    // Ensure we don't go below minimum speed or above current max
                    if (newSpeed < minStartSpeed) {
                        newSpeed = minStartSpeed;
                    }

                    // If calculated speed is very low, just stop
                    if (newSpeed <= minStartSpeed && stepsRemaining < 50) {
                        newSpeed = 0;
                        state = STEPPER_STATE_IDLE;
                        continuousMode = false;  // Clear continuous mode flag
                        if (stepTimer) stepTimer->pause();
                        DEBUG_PRINTLN("STOPPING: near end, stopping now");
                    }
                }
            }
            break;

        default:
            break;
    }

    // Update oversampling ticks if speed changed
    if (newSpeed != currentSpeed) {
        currentSpeed = newSpeed;
        ticksPerStep = speedToTimerPeriod(newSpeed);
        // Note: tickCounter continues from current value for smooth transition
    }
}

void StepperTimer::handleStepInterrupt() {
    // This ISR is called at constant 10kHz
    // Use oversampling to control step rate

    if (state == STEPPER_STATE_IDLE) return;

    // Increment tick counter
    tickCounter++;

    // Check if it's time to generate a step pulse
    if (tickCounter >= ticksPerStep) {
        tickCounter = 0;  // Reset counter for next step

        // Generate step pulse only if we have steps remaining or in continuous mode
        if (stepCounter > 0 || continuousMode) {
            // Rising edge
            digitalWrite(STEP_PIN, HIGH);

            // Short delay (inline for speed)
            for (volatile int i = 0; i < 10; i++);

            // Falling edge
            digitalWrite(STEP_PIN, LOW);

            // Update position
            currentPos += direction;

            // Decrement step counter (if not in continuous mode)
            if (!continuousMode && stepCounter > 0) {
                stepCounter--;
            }
        }

        // Stop timer if movement complete
        if (!continuousMode && stepCounter == 0 && state != STEPPER_STATE_IDLE) {
            if (stepTimer) stepTimer->pause();  // Disable timer
            state = STEPPER_STATE_IDLE;
        }
    }
}
