// Minimal FilterWheel interface with basic TMC2209 support
#pragma once
#include <Arduino.h>

#include "config.h"

#include "hall_sensor.h"
#include "button.h"
#include "TMCStepper.h"
#include "SparkFun_External_EEPROM.h"
#include "calibration_data.h"

#include "error_codes.h"

class ProtocolOpenWheel;

// State machine states
enum FilterWheelState {
  STATE_INIT,         // Initialization
  STATE_IDLE,         // Waiting for commands
  STATE_MOVING,       // Moving to target slot
  STATE_HOMING,       // Performing homing sequence
  STATE_CALIBRATING,  // Performing calibration
  STATE_ERROR         // Error state
};

class FilterWheel {
public:
  FilterWheel();
  void start();
  void loop();

  // Public command methods. Return FW_OK or the reason the command was rejected.
  FwError moveToSlot(uint8_t slot);
  FwError goHome();
  FwError calibrate();
  FwError moveSteps(int32_t steps);

  // State queries
  FilterWheelState getState() const { return state; }
  uint8_t getCurrentSlot() const { return currentSlot; }
  bool isMoving() const { return state == STATE_MOVING || state == STATE_HOMING; }
  bool isCalibrated() const { return calibration.isValid(); }

  // Slot as reported to the host: -1 while the position is not known (moving, homing, calibrating, init)
  int16_t getReportedSlot() const;
  // Target of a slot move, -1 if none (also -1 for step moves and homing)
  int16_t getTargetSlot() const;
  // STATUS flag bits: 1=calibrated, 2=position known (homed), 4=button held at boot, 8=EEPROM fault
  uint8_t getFlags() const;
  // Sticky device fault (FW_OK if none). Stays set after the ERROR state ends until cleared.
  FwError getError() const { return lastError; }
  void clearError() { lastError = FW_OK; }

  // Movement control
  void stopMovement();

  // EEPROM and calibration management
  ExternalEEPROM eeprom;
  CalibrationManager calibration;

private:
  void init_hardware();

  // State machine handlers
  void handleStateInit();
  void handleStateIdle();
  void handleStateMoving();
  void handleStateHoming();
  void handleStateCalibrating();
  void handleStateError();

  // Error handling
  void raiseError(FwError code);                 // sticky fault: stop motor, enter STATE_ERROR
  FwError classifyTimeout(FwError timeoutCode);  // refine a timeout: driver fault / no hall pulses / plain timeout
  bool checkDriver();                            // TMC2209 answers and reports no overtemp/short

  // Helper methods
  uint8_t findHomeSlot();
  bool detectSlotByPulse(uint16_t pulseLength, uint8_t& slotNumber);
  void handleButtonEvents();
  void rebootToDFU();  // Reboot into DFU bootloader mode

  // State machine variables
  FilterWheelState state;
  uint8_t currentSlot;
  uint8_t targetSlot;

  // Fault tracking
  FwError lastError;
  bool positionValid;       // true once a move/home has arrived; cleared on halt, error, calibration
  bool eepromFault;
  uint16_t pulsesSeen;      // hall pulses seen during the current move/home/calibration
  unsigned long lastDriverCheck;

  // Movement tracking
  bool inPulse;
  uint32_t pulseStartPosition;
  unsigned long stateStartTime;
  bool applyingOffset;  // Flag to indicate offset movement is in progress (deceleration + offset combined)
  bool stepMovementMode;  // Flag to indicate simple step movement (no magnet detection)

  // LED slot number blinking
  bool blinkingSlotNumber;
  uint8_t blinksRemaining;
  unsigned long lastBlinkTime;
  bool blinkLedState;

  // Calibration pass tracking for LED blinking
  uint8_t calibrationPass;

  // Buttons
  Button button;          // CW button (next slot)
  Button buttonCCW;       // CCW button (previous slot)
  bool buttonPressedDuringBoot;

  // Protocol handler
  ProtocolOpenWheel* protocol;

  // Allow protocol handler to access private members
  friend class ProtocolOpenWheel;
};
