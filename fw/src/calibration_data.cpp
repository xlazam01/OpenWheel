// Calibration data implementation
#include "calibration_data.h"


CalibrationManager::CalibrationManager() : loaded(false) {
    clear();
}


void CalibrationManager::clear() {
    memset(&data, 0, sizeof(CalibrationData));
    data.magic = 0;  // Invalid magic number
    data.version = 2;
    data.numSlots = 0;
    data.maxSteps = MAXSTEPS;
    data.startupBehavior = (STARTUP_BEHAVIOR == STARTUP_BEHAVIOR_HOME) ? 1 : 0;

    // Initialize offsets to 0
    data.baseOffset = 0;
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        data.userOffsets[i] = 0;
        data.opticalOffsets[i] = 0;
    }

    loaded = false;
}

uint16_t CalibrationManager::crc16_update(uint16_t crc, uint8_t byte) {
    // CRC16-CCITT (0x1021 polynomial)
    crc ^= ((uint16_t)byte << 8);
    for (uint8_t i = 0; i < 8; i++) {
        if (crc & 0x8000) {
            crc = (crc << 1) ^ 0x1021;
        } else {
            crc = crc << 1;
        }
    }
    return crc;
}

uint16_t CalibrationManager::calculateCRC() const {
    uint16_t crc = 0xFFFF;  // Initial value
    uint8_t* dataPtr = (uint8_t*)&data;

    // Calculate CRC for all fields except the CRC field itself
    size_t length = sizeof(CalibrationData) - sizeof(data.crc);

    for (size_t i = 0; i < length; i++) {
        crc = crc16_update(crc, dataPtr[i]);
    }

    return crc;
}

bool CalibrationManager::load() {
    DEBUG_PRINTLN("Loading calibration from EEPROM...");

    // Read calibration data from EEPROM byte by byte
    uint8_t* dataPtr = (uint8_t*)&data;
    uint16_t addr = EEPROM_CALIBRATION_ADDR;
    size_t dataSize = sizeof(CalibrationData);

    DEBUG_PRINT("  Reading ");
    DEBUG_PRINT(dataSize);
    DEBUG_PRINT(" bytes from address 0x");
    DEBUG_PRINTLN(addr, HEX);

    for (size_t i = 0; i < dataSize; i++) {
        dataPtr[i] = eepromMem.read(addr + i);
    }
    delay(5);  // Small delay after read

    DEBUG_PRINTLN("  EEPROM read complete");

    loaded = true;
    bool valid = isValid();

    if (valid) {
        DEBUG_PRINTLN("  Calibration data is valid");
    } else {
        DEBUG_PRINTLN("  Calibration data is INVALID");
    }

    return valid;
}

bool CalibrationManager::save() {
    DEBUG_PRINTLN("Saving calibration to EEPROM...");
    DEBUG_PRINT("  Structure size: ");
    DEBUG_PRINT(sizeof(CalibrationData));
    DEBUG_PRINTLN(" bytes");

    // Set magic number and version
    data.magic = CALIBRATION_MAGIC;
    data.version = 2;

    // Calculate and store CRC
    data.crc = calculateCRC();
    DEBUG_PRINT("  Calculated CRC before save: 0x");
    DEBUG_PRINTLN(data.crc, HEX);

    // Write to EEPROM byte by byte
    uint8_t* dataPtr = (uint8_t*)&data;
    uint16_t addr = EEPROM_CALIBRATION_ADDR;
    size_t dataSize = sizeof(CalibrationData);

    DEBUG_PRINT("  Writing ");
    DEBUG_PRINT(dataSize);
    DEBUG_PRINT(" bytes to EEPROM at address 0x");
    DEBUG_PRINTLN(addr, HEX);

    for (size_t i = 0; i < dataSize; i++) {
        eepromMem.write(addr + i, dataPtr[i]);
        delay(5);   // Small delay between writes iisu with fake STM32
    }
    delay(10);  // Wait for write to complete

    DEBUG_PRINTLN("  EEPROM write complete");

    // Verify write by reading back
    DEBUG_PRINTLN("  Verifying EEPROM write...");
    CalibrationData verifyData;
    uint8_t* verifyPtr = (uint8_t*)&verifyData;

    for (size_t i = 0; i < dataSize; i++) {
        verifyPtr[i] = eepromMem.read(addr + i);
    }

    if (memcmp(&data, &verifyData, dataSize) == 0) {
        DEBUG_PRINTLN("  EEPROM verification: SUCCESS");
    } else {
        DEBUG_PRINTLN("  EEPROM verification: FAILED - data mismatch!");
        DEBUG_PRINT("  Written magic: 0x");
        DEBUG_PRINT(data.magic, HEX);
        DEBUG_PRINT(", Read magic: 0x");
        DEBUG_PRINTLN(verifyData.magic, HEX);
        DEBUG_PRINT("  Written CRC: 0x");
        DEBUG_PRINT(data.crc, HEX);
        DEBUG_PRINT(", Read CRC: 0x");
        DEBUG_PRINTLN(verifyData.crc, HEX);
    }

    loaded = true;
    return true;
}

bool CalibrationManager::isValid() const {
    if (!loaded) {
        DEBUG_PRINTLN("  Validation failed: Data not loaded");
        return false;
    }

    // Check magic number
    if (data.magic != CALIBRATION_MAGIC) {
        DEBUG_PRINT("  Validation failed: Magic mismatch (expected 0x");
        DEBUG_PRINT(CALIBRATION_MAGIC, HEX);
        DEBUG_PRINT(", got 0x");
        DEBUG_PRINT(data.magic, HEX);
        DEBUG_PRINTLN(")");
        return false;
    }

    // Check version (accept version 1 or 2)
    if (data.version != 1 && data.version != 2) {
        DEBUG_PRINT("  Validation failed: Version mismatch (expected 1 or 2, got ");
        DEBUG_PRINT(data.version);
        DEBUG_PRINTLN(")");
        return false;
    }

    // Check number of slots is in valid range
    if (data.numSlots < 3 || data.numSlots > MAX_SLOTS) {
        DEBUG_PRINT("  Validation failed: Invalid slot count (");
        DEBUG_PRINT(data.numSlots);
        DEBUG_PRINTLN(")");
        return false;
    }

    // Verify CRC
    uint16_t calculatedCRC = calculateCRC();
    if (data.crc != calculatedCRC) {
        DEBUG_PRINT("  Validation failed: CRC mismatch (stored: 0x");
        DEBUG_PRINT(data.crc, HEX);
        DEBUG_PRINT(", calculated: 0x");
        DEBUG_PRINT(calculatedCRC, HEX);
        DEBUG_PRINTLN(")");
        return false;
    }

    return true;
}

uint16_t CalibrationManager::getPulseLength(uint8_t slot) const {
    if (slot >= MAX_SLOTS) {
        return 0;
    }
    return data.pulseLengths[slot];
}

void CalibrationManager::setPulseLength(uint8_t slot, uint16_t length) {
    if (slot < MAX_SLOTS) {
        data.pulseLengths[slot] = length;
    }
}

// User offset methods (per-slot fine-tuning, user-adjustable)
int16_t CalibrationManager::getUserOffset(uint8_t slot) const {
    if (slot >= MAX_SLOTS) {
        return 0;
    }
    return data.userOffsets[slot];
}

void CalibrationManager::setUserOffset(uint8_t slot, int16_t offset) {
    if (slot < MAX_SLOTS) {
        data.userOffsets[slot] = offset;
    }
}

// Optical offset methods (per-filter optical calibration)
int16_t CalibrationManager::getOpticalOffset(uint8_t slot) const {
    if (slot >= MAX_SLOTS) {
        return 0;
    }
    return data.opticalOffsets[slot];
}

void CalibrationManager::setOpticalOffset(uint8_t slot, int16_t offset) {
    if (slot < MAX_SLOTS) {
        data.opticalOffsets[slot] = offset;
    }
}

// Get total offset (base + user for specific slot)
int16_t CalibrationManager::getTotalOffset(uint8_t slot) const {
    if (slot >= MAX_SLOTS) {
        return 0;
    }
    return data.baseOffset + data.userOffsets[slot];
}

// Legacy compatibility
uint16_t CalibrationManager::getOffset(uint8_t slot) const {
    if (slot >= MAX_SLOTS) {
        return 0;
    }
    // Return base offset (single value for all slots)
    return (uint16_t)data.baseOffset;
}

void CalibrationManager::setOffset(uint8_t slot, int16_t offset) {
    // Legacy method sets the base offset (ignore slot parameter)
    data.baseOffset = (int16_t)offset;
}

uint8_t CalibrationManager::findHomeSlot() const {
    if (data.numSlots == 0) {
        return 0;
    }

    uint8_t homeSlot = 0;
    uint16_t shortestPulse = data.pulseLengths[0];

    for (uint8_t i = 1; i < data.numSlots; i++) {
        if (data.pulseLengths[i] < shortestPulse) {
            shortestPulse = data.pulseLengths[i];
            homeSlot = i;
        }
    }

    return homeSlot;
}

void CalibrationManager::printCalibration() {
    DEBUG_PRINTLN("\n=== Calibration Data ===");
    DEBUG_PRINT("Magic: 0x");
    DEBUG_PRINTLN(data.magic, HEX);
    DEBUG_PRINT("Version: ");
    DEBUG_PRINTLN(data.version);
    DEBUG_PRINT("Number of slots: ");
    DEBUG_PRINTLN(data.numSlots);
    DEBUG_PRINT("Max steps: ");
    DEBUG_PRINTLN(data.maxSteps);

    DEBUG_PRINT("Base offset (geometry): ");
    DEBUG_PRINT(data.baseOffset);
    DEBUG_PRINTLN(" steps");

    DEBUG_PRINTLN("\nSlot data:");
    for (uint8_t i = 0; i < data.numSlots; i++) {
        DEBUG_PRINT("  Slot ");
        DEBUG_PRINT(i);
        DEBUG_PRINT(": Pulse=");
        DEBUG_PRINT(data.pulseLengths[i]);
        DEBUG_PRINT(" steps, UserOffset=");
        DEBUG_PRINT(data.userOffsets[i]);
        DEBUG_PRINT(" steps, Total=");
        DEBUG_PRINT(getTotalOffset(i));
        DEBUG_PRINTLN(" steps");
    }

    uint8_t homeSlot = findHomeSlot();
    DEBUG_PRINT("\nHome slot: ");
    DEBUG_PRINT(homeSlot);
    DEBUG_PRINT(" (pulse length: ");
    DEBUG_PRINT(data.pulseLengths[homeSlot]);
    DEBUG_PRINTLN(" steps)");

    DEBUG_PRINT("CRC: 0x");
    DEBUG_PRINTLN(data.crc, HEX);
    DEBUG_PRINTLN("========================\n");
}
