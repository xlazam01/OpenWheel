// Timer-based stepper motor driver for STM32G0B1
// Uses hardware timer interrupt for pulse generation
#pragma once

#include <Arduino.h>

// Movement states
enum StepperState {
    STEPPER_STATE_IDLE = 0,
    STEPPER_STATE_ACCEL,      // Accelerating
    STEPPER_STATE_CONST,      // Constant speed
    STEPPER_STATE_DECEL,      // Decelerating
    STEPPER_STATE_STOPPING    // Emergency stop with deceleration
};

class StepperTimer {
public:
    StepperTimer();

    // Initialize timer and pins
    void begin();

    // Movement commands (called from main loop)
    void move(int32_t steps);           // Relative move with acceleration
    void runConstSpeed(int16_t speed);  // Continuous movement at constant speed (signed: +forward, -backward)
    void stop(bool decelerate = true);   // Stop movement (decelerate=true: smooth stop, false: instant stop)
    void emergencyStop();                // Immediate stop (no deceleration) - same as stop(false)

    // Speed and acceleration settings
    void setMaxSpeed(uint16_t stepsPerSec);
    void setAcceleration(uint16_t stepsPerSecSq);

    // Status queries
    int32_t distanceToGo() const;
    int32_t currentPosition() const;
    bool isRunning() const;
    StepperState getState() const;

    // Position management
    void setCurrentPosition(int32_t pos);

    // Called from main loop to update motion profile
    void update();

    // Called from ISR to generate step pulses (public for ISR access)
    void handleStepInterrupt();

private:
    // Configuration
    uint16_t maxSpeed;          // Steps per second
    uint16_t acceleration;      // Steps per second^2

    // Current state
    volatile StepperState state;
    volatile int32_t currentPos;
    volatile int32_t targetPos;
    volatile uint16_t currentSpeed;  // Current speed in steps/sec
    volatile uint16_t targetSpeed;   // Target speed for current phase

    // Timer period management
    volatile uint16_t timerPeriod;   // Timer reload value for current speed
    volatile uint16_t stepCounter;   // Steps remaining in current movement

    // Oversampling for smooth acceleration (10kHz timer)
    volatile uint16_t ticksPerStep;  // Number of timer ticks between steps (at 10kHz)
    volatile uint16_t tickCounter;   // Counter for oversampling

    // Direction
    volatile int8_t direction;       // 1 = forward, -1 = backward

    // Acceleration tracking
    volatile uint32_t accelSteps;    // Steps during acceleration
    volatile uint32_t decelSteps;    // Steps during deceleration
    volatile uint32_t constSteps;    // Steps at constant speed

    // Continuous run mode
    volatile bool continuousMode;    // True for runConstSpeed mode

    // Time tracking for acceleration
    uint32_t lastUpdateTime;         // Last time update() was called (milliseconds)

    // Calculate timer period from speed
    uint16_t speedToTimerPeriod(uint16_t speed);
};

// Global instance (defined in stepper_timer.cpp)
extern StepperTimer stepper;
