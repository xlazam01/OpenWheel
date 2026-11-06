// STM32G0B1 Board Configuration
// Based on WeAct Studio STM32G0B1 development board schematic
#pragma once
#include <Arduino.h>

// Firmware information
#define FIRMWARE_NAME    "OpenWheel"
#define FIRMWARE_VERSION "FW1.4.0"

// Board revision (set by the PlatformIO environment)
//   1 = prototype board: no +5V sense divider, TMC2209 INDEX not connected
//   2 = redesigned board: +5V sense divider on PA0, TMC2209 INDEX on PA6
#ifndef HW_REV
#define HW_REV 1
#endif
#define HAS_V5_SENSE   (HW_REV >= 2)
#define HAS_TMC_INDEX  (HW_REV >= 2)

// USB CDC Serial Configuration
// STM32G0B1 has native USB support - use USB CDC for Serial communication
// This allows the device to appear as a virtual COM port when connected via USB
#define ENABLE_USB_CDC 1  // 1=enable USB CDC serial, 0=use UART

// Debug output control
#define DEBUG_ENABLED 0  // 1=enable debug messages, 0=disable

// Debug print macros
// On STM32G0B1 with USB CDC enabled, Serial goes over USB (SerialUSB)
#if DEBUG_ENABLED
  #if ENABLE_USB_CDC
    #define DEBUG_PRINT(...)    SerialUSB.print(__VA_ARGS__)
    #define DEBUG_PRINTLN(...)  SerialUSB.println(__VA_ARGS__)
    #define DEBUG_PRINTF(...)   SerialUSB.printf(__VA_ARGS__)
  #else
    #define DEBUG_PRINT(...)    Serial.print(__VA_ARGS__)
    #define DEBUG_PRINTLN(...)  Serial.println(__VA_ARGS__)
    #define DEBUG_PRINTF(...)   Serial.printf(__VA_ARGS__)
  #endif
#else
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTLN(...)
  #define DEBUG_PRINTF(...)
#endif

// Serial protocol: OpenWheel (":NN[params]#")
//   2 = every command is acknowledged
//   3 = identify first: only :25# and :32# until the host has identified the device (ERR,16)
#define PROTOCOL_VERSION 3
#define DEVICE_MODEL     "G0B1"

// Serial speed (UART builds; USB CDC ignores it)
#define SERIALPORTSPEED 115200

// Serial port abstraction for protocol communication
// This allows protocols to work with both USB CDC and hardware UART
#if ENABLE_USB_CDC
  #define PROTOCOL_SERIAL SerialUSB  // Use USB CDC for protocol commands
#else
  #define PROTOCOL_SERIAL Serial     // Use hardware UART
#endif

// External EEPROM (I2C) settings
#define EEPROMSIZE 8192 // 24AA64 (8Kbit = 8192 bytes)
#define EEPROM_ADDRESS 0b1010000 // 0x50 (A2=A1=A0=0)

// Startup behavior options
#define STARTUP_BEHAVIOR_STAY       0  // Stay at current position
#define STARTUP_BEHAVIOR_VERIFY     1  // Verify current position by detecting next magnet
#define STARTUP_BEHAVIOR_HOME       2  // Always go to home position

// Startup behavior configuration
#define STARTUP_BEHAVIOR STARTUP_BEHAVIOR_HOME  // Default: find home on startup

// Motor rotation direction
#define ROTATION_DIRECTION_CW   0  // Clockwise rotation
#define ROTATION_DIRECTION_CCW  1  // Counter-clockwise rotation
#define ROTATION_DIRECTION ROTATION_DIRECTION_CCW  // Default: counter-clockwise

// Hall sensor
#define HALLACTIVETYPE 2     // 1=active HIGH, 2=active LOW

// Limits
#define MAXSTEPS 100000L
#define MAX_SLOTS 9
#define BASE_OFFSET 308  // Base offset for wheel geometry (in steps)

// Motor current settings (mA)
// HW_REV 1 keeps the TMC2209 default i_scale_analog = 1: VREF is unconnected, so the real current
// is this value scaled by whatever VREF floats to. That setup is proven on the prototype. A true
// 400 mA (i_scale_analog = 0) browned out the driver: the whole board runs from USB 5V behind a
// 0.5 A polyfuse. HW_REV 2 uses the internal reference with a lower, still to be verified current.
#if HW_REV >= 2
#define DEFAULT_RUN_CURRENT_MA 250
#else
#define DEFAULT_RUN_CURRENT_MA 400
#endif
#define DEFAULT_HOLD_CURRENT_MA 100

// TMC2209 Driver Configuration
#define TMC_MICROSTEPS 0           // Microstepping mode (0, 2, 4, 8, 16, 32, 64, 128, 256)
#define TMC_TOFF 5                 // Off time setting (1-15). Higher = smoother but noisier. 0=driver disabled
#define TMC_HOLD_MULTIPLIER 0.5    // Hold current multiplier (0.0-1.0). Reduces current when motor is idle
#define TMC_PWM_AUTOSCALE 1        // Enable automatic PWM amplitude scaling (0=disabled, 1=enabled)

// StallGuard4 (stall -> DIAG). Threshold SGTHRS is stored in the EEPROM (:42), 0 = detection off.
// A stall is flagged when SG_RESULT <= 2 * SGTHRS while the motor runs faster than STALL_MIN_SPEED.
#define STALL_MIN_SPEED   500      // steps/s; below this the driver ignores StallGuard (TCOOLTHRS)
#define STALL_BLANK_MS    200      // ignore DIAG this long after the motor starts (StealthChop settles)
#define SG_SAMPLE_MS      20       // SG_RESULT sampling period during :21 step moves (tuning data)

// Housekeeping (MCU temperature, +5V rail)
#define V5_DIVIDER        2        // +5V sense divider ratio (10k / 10k)
#define V5_LOW_MV         4750     // warning below this (TMC2209 VS minimum)
#define HK_IDLE_MS        1000     // full housekeeping sample period
#define HK_MOVE_MS        5        // +5V sample period while the motor runs
// ---------------------------------------------------------------------------
// Pin mapping (STM32G0B1 - WeAct Studio board compatible)
// Update these if you change wiring
// ---------------------------------------------------------------------------

// TMC2209 UART (USART2)
#define SW_RX            PA3  // USART2 RX (TMC2209 UART)
#define SW_TX            PA2  // USART2 TX (TMC2209 UART)
#define SERIAL_PORT      Serial2  // HardwareSerial port for TMC2209
#define DRIVER_ADDRESS   0b00  // TMC2209 Driver address according to MS1 and MS2

// TMC2209 Control Pins
#define EN_PIN           PB0  // Enable (active LOW)
#define DIR_PIN          PB1  // Direction
#define STEP_PIN         PB2  // Step
#define DIAG_PIN         PA7  // Diagnostic output from TMC2209 (driver error or stall)
#define INDEX_PIN        PA6  // TMC2209 INDEX, configured as overtemperature prewarning (HW_REV 2)

// +5V rail sense (HW_REV 2): +5V / V5_DIVIDER on ADC_IN0
#define PIN_V5_SENSE     PA0

// Unused TMC pins (SPI mode - not used in UART mode)
#define CS_PIN           NULL  // Chip select
#define SW_MOSI          NULL  // Software Master Out Slave In (MOSI)
#define SW_MISO          NULL  // Software Master In Slave Out (MISO)
#define SW_SCK           NULL  // Software Slave Clock (SCK)

#define R_SENSE          0.11f // Match to your driver
                               // SilentStepStick series use 0.11
                               // UltiMachine Einsy and Archim2 boards use 0.2
                               // Panucatt BSD2660 uses 0.1
                               // Watterott TMC5160 uses 0.075

// Stepper motor performance settings
#define STEPPER_MAX_SPEED       1000   // Maximum motor speed (steps/sec)
#define STEPPER_SPEED           1000   // Current speed (steps/sec)
#define STEPPER_ACCEL           2000   // Stepper acceleration (steps/sec²)

// I2C (for external EEPROM)
// Note: STM32G0B1 uses I2C1 with AF6 on PA9/PA10
#define PIN_I2C_SCL      PA9   // I2C1 SCL (AF6)
#define PIN_I2C_SDA      PA10  // I2C1 SDA (AF6)

// USB (Native USB on STM32G0B1)
// Note: PA11/PA12 are dedicated USB pins on WeAct board
#define PIN_USB_DM       PA11  // USB D- (PA11)
#define PIN_USB_DP       PA12  // USB D+ (PA12)

// Hall sensor
#define PIN_HALL         PA1   // Hall effect sensor input

// Status LEDs (active LOW)
#define PIN_STATUS_LED1  PB12  // Status LED 1 (active LOW)
#define PIN_STATUS_LED2  PB4   // Status LED 2 (active LOW)

// User buttons
#define PIN_BUTTON       PB13  // Button for CW turn
#define PIN_BUTTON_CCW   PA14  // Button for CCW turn (shared with SWCLK/BOOT0)
                               // WARNING: PA14 is SWCLK - using it as button may interfere with debugging
                               // Consider external pull-up and careful schematic design

// Timer selection for stepper control
// STM32G0B1 has TIM1, TIM2, TIM3, TIM6, TIM7, TIM14, TIM15, TIM16, TIM17
// STM32G0B1 has no TIM4 - TIM3 is used
#define STEPPER_TIMER    TIM3  // Timer for stepper motor control 
