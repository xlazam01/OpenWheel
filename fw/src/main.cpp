// Minimal entrypoint providing setup() and loop()
#include <Arduino.h>
#include <Wire.h>
//#include <HardwareSerial.h>

#include "SparkFun_External_EEPROM.h"
#include "filterwheel.h"
#include "hall_sensor.h"

#include "config.h"

#include "calibration_data.h"

// Disable USB CDC DTR gating so responses are sent even when the host opens
// the port with DTR=false (ASCOM driver ResetControllerOnConnect=false default).
#if ENABLE_USB_CDC
extern "C" void CDC_enableDTR(bool enable);
#endif


FilterWheel myFilterWheel;



void testCalibrationData() {
  DEBUG_PRINTLN("\n=== Calibration Data Test Start ===");

  // Test 1: Clear and save empty calibration
  DEBUG_PRINT("1. Clearing calibration data... ");
  myFilterWheel.calibration.clear();
  DEBUG_PRINTLN("OK");

  // Test 2: Check that cleared data is invalid
  DEBUG_PRINT("2. Verifying cleared data is invalid... ");
  if (!myFilterWheel.calibration.isValid()) {
    DEBUG_PRINTLN("OK");
  } else {
    DEBUG_PRINTLN("FAILED (should be invalid)");
  }

  // Test 3: Create sample calibration data
  DEBUG_PRINT("3. Creating sample calibration data... ");
  myFilterWheel.calibration.setNumSlots(5);
  myFilterWheel.calibration.setMaxSteps(MAXSTEPS);

  // Set pulse lengths (in steps) - home slot has shortest pulse
  myFilterWheel.calibration.setPulseLength(0, 1200);  // Home slot
  myFilterWheel.calibration.setPulseLength(1, 1500);
  myFilterWheel.calibration.setPulseLength(2, 1800);
  myFilterWheel.calibration.setPulseLength(3, 2100);
  myFilterWheel.calibration.setPulseLength(4, 2400);

  // Set offsets (in steps)
  myFilterWheel.calibration.setOffset(0, -50);
  myFilterWheel.calibration.setOffset(1, 30);
  myFilterWheel.calibration.setOffset(2, -20);
  myFilterWheel.calibration.setOffset(3, 10);
  myFilterWheel.calibration.setOffset(4, 40);
  DEBUG_PRINTLN("OK");

  // Test 4: Save to EEPROM
  DEBUG_PRINT("4. Saving calibration to EEPROM... ");
  Serial.flush();  // Ensure output is sent before potentially hanging
  if (myFilterWheel.calibration.save()) {
    DEBUG_PRINTLN("OK");
  } else {
    DEBUG_PRINTLN("FAILED");
    return;
  }

  // Test 5: Clear local data
  DEBUG_PRINT("5. Clearing local calibration data... ");
  myFilterWheel.calibration.clear();
  DEBUG_PRINTLN("OK");

  // Test 6: Load from EEPROM
  DEBUG_PRINT("6. Loading calibration from EEPROM... ");
  if (myFilterWheel.calibration.load()) {
    DEBUG_PRINTLN("OK");
  } else {
    DEBUG_PRINTLN("FAILED");
    return;
  }

  // Test 7: Validate loaded data
  DEBUG_PRINT("7. Validating loaded data... ");
  if (myFilterWheel.calibration.isValid()) {
    DEBUG_PRINTLN("OK");
  } else {
    DEBUG_PRINTLN("FAILED");
    return;
  }

  // Test 8: Verify data integrity
  DEBUG_PRINT("8. Verifying data integrity... ");
  bool dataOK = true;
  if (myFilterWheel.calibration.getNumSlots() != 5) dataOK = false;
  if (myFilterWheel.calibration.getMaxSteps() != MAXSTEPS) dataOK = false;
  if (myFilterWheel.calibration.getPulseLength(0) != 1200) dataOK = false;
  if (myFilterWheel.calibration.getPulseLength(4) != 2400) dataOK = false;
  if (myFilterWheel.calibration.getOffset(0) != -50) dataOK = false;
  if (myFilterWheel.calibration.getOffset(4) != 40) dataOK = false;

  if (dataOK) {
    DEBUG_PRINTLN("OK");
  } else {
    DEBUG_PRINTLN("FAILED (data mismatch)");
    return;
  }

  // Test 9: Find home slot
  DEBUG_PRINT("9. Finding home slot... ");
  uint8_t homeSlot = myFilterWheel.calibration.findHomeSlot();
  if (homeSlot == 0) {
    DEBUG_PRINT("OK (slot ");
    DEBUG_PRINT(homeSlot);
    DEBUG_PRINTLN(")");
  } else {
    DEBUG_PRINT("FAILED (expected 0, got ");
    DEBUG_PRINT(homeSlot);
    DEBUG_PRINTLN(")");
  }

  // Test 10: Print calibration data
  DEBUG_PRINTLN("10. Printing calibration data:");
  myFilterWheel.calibration.printCalibration();

  DEBUG_PRINTLN("=== Calibration Data Test Complete ===\n");
}

void setup() {
  // Initialize Serial communication
  // With USB CDC (default): SerialUSB is used for protocol and debug
#if ENABLE_USB_CDC
  CDC_enableDTR(false);
  SerialUSB.begin(SERIALPORTSPEED);
  // Wait for USB CDC to enumerate
  delay(1500);  // Longer delay for USB enumeration
#else
  Serial.begin(SERIALPORTSPEED);
  delay(500);
#endif

  DEBUG_PRINTLN("Filter Wheel Starting...");
  DEBUG_PRINT("Firmware: ");
  DEBUG_PRINT(FIRMWARE_NAME);
  DEBUG_PRINT(" ");
  DEBUG_PRINTLN(FIRMWARE_VERSION);

  // Turn on Status1 LED at startup (active low)
  pinMode(PIN_STATUS_LED1, OUTPUT);
  digitalWrite(PIN_STATUS_LED1, LOW);

  // Initialize I2C for EEPROM
  // STM32 requires explicit pin configuration for non-default I2C pins
Wire.setSDA(PIN_I2C_SDA);  // PA10
  Wire.setSCL(PIN_I2C_SCL);  // PA9
  Wire.begin();

  // Start FilterWheel module (initializes hardware, EEPROM, and calibration)
  myFilterWheel.start();

  // Run calibration data test
  //testCalibrationData();
}

void loop() {
  myFilterWheel.loop();
}
