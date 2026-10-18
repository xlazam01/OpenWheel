// Calibration data structure for filter wheel
#pragma once

#include <Arduino.h>

#include "config.h"

#include "SparkFun_External_EEPROM.h"

// Magic number to identify valid calibration data
#define CALIBRATION_MAGIC 0x4657  // "FW" in hex

// EEPROM memory map
#define EEPROM_CALIBRATION_ADDR 0  // Start address for calibration data

// Calibration data structure
struct CalibrationData {
    // Header
    uint16_t magic;           // 2 Magic number to identify valid data (0x4657)
    uint8_t version;          // 1 Data structure version (currently 2)
    uint8_t numSlots;         // 1 Number of slots detected (3-9)

    // Wheel configuration
    uint32_t maxSteps;        // 4 Maximum steps for full rotation (limit)
    uint8_t startupBehavior;  // 1 Startup behavior (0=none, 1=home on startup)

    // Wheel geometry
    int16_t baseOffset;                // 2 Base offset for wheel geometry (single value for all slots)

    // Per-slot data (fixed array for max slots)
    uint16_t pulseLengths[MAX_SLOTS];  // 2 Pulse length for each slot (in steps)
    int16_t userOffsets[MAX_SLOTS];    // 2 User offset for each slot (fine-tuning, adjustable)
    int16_t opticalOffsets[MAX_SLOTS]; // 2 Optical offset for each filter (per-filter calibration)

    // Data integrity
    uint16_t crc;             // 2 CRC16 checksum of all data above
} __attribute__((packed));

// Function prototypes
class CalibrationManager {
public:
    CalibrationManager();

    // Load calibration data from EEPROM
    bool load();

    // Save calibration data to EEPROM
    bool save();

    // Validate loaded data (check magic number and CRC)
    bool isValid() const;

    // Clear/reset calibration data
    void clear();

    // Get/Set methods
    uint8_t getNumSlots() const { return data.numSlots; }
    void setNumSlots(uint8_t slots) { data.numSlots = slots; }

    uint32_t getMaxSteps() const { return data.maxSteps; }
    void setMaxSteps(uint32_t steps) { data.maxSteps = steps; }

    uint8_t getStartupBehavior() const { return data.startupBehavior; }
    void setStartupBehavior(uint8_t behavior) { data.startupBehavior = behavior; }

    uint16_t getPulseLength(uint8_t slot) const;
    void setPulseLength(uint8_t slot, uint16_t length);

    // Base offset methods (single value for wheel geometry)
    int16_t getBaseOffset() const { return data.baseOffset; }
    void setBaseOffset(int16_t offset) { data.baseOffset = offset; }

    // User offset methods (per-slot fine-tuning, user-adjustable)
    int16_t getUserOffset(uint8_t slot) const;
    void setUserOffset(uint8_t slot, int16_t offset);

    // Optical offset methods (per-filter optical calibration)
    int16_t getOpticalOffset(uint8_t slot) const;
    void setOpticalOffset(uint8_t slot, int16_t offset);

    // Get total offset (base + user for specific slot)
    int16_t getTotalOffset(uint8_t slot) const;

    // Legacy compatibility (uses baseOffset for slot 0, user offset for others)
    uint16_t getOffset(uint8_t slot) const;
    void setOffset(uint8_t slot, int16_t offset);

    // Find home slot (shortest pulse)
    uint8_t findHomeSlot() const;

    // Print calibration data to Serial
    void printCalibration();

private:
    CalibrationData data;
    ExternalEEPROM eepromMem;
    bool loaded;

    // Calculate CRC16 for the data structure (excluding CRC field itself)
    uint16_t calculateCRC() const;

    // CRC16-CCITT implementation
    static uint16_t crc16_update(uint16_t crc, uint8_t byte);
};
