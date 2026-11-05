// Filter names and focus offsets, stored in the external EEPROM in their own region
// (separate magic + CRC), so they survive a calibration reset.
#pragma once

#include <Arduino.h>
#include "config.h"
#include "SparkFun_External_EEPROM.h"

#define FILTER_CONFIG_MAGIC  0x4E53   // "NS"
#define FILTER_CONFIG_ADDR   256      // calibration data lives at 0..~70
#define FILTER_NAME_LEN      16       // 15 characters + terminator

// Version 2 appended the device settings after the focus offsets; a version 1 record (same
// layout up to focusOffsets, then its CRC) is migrated on load.
struct FilterConfigData {
    uint16_t magic;
    uint8_t  version;                              // 2
    uint8_t  stallThreshold;                       // TMC2209 SGTHRS, 0 = stall detection off (was reserved, always 0)
    char     names[MAX_SLOTS][FILTER_NAME_LEN];    // empty string = no name set
    int16_t  focusOffsets[MAX_SLOTS];
    uint8_t  ledBrightness;                        // 0..100 %   (version 2)
    uint8_t  reserved[3];                          // 0          (version 2)
    uint16_t crc;                                  // CRC16-CCITT of everything above
} __attribute__((packed));

#define FILTER_CONFIG_V1_SIZE (sizeof(FilterConfigData) - 4)   // v1 had no ledBrightness / reserved

class FilterConfig {
public:
    explicit FilterConfig(ExternalEEPROM* eeprom);

    // Read from EEPROM. Returns true if a valid record was found; otherwise the
    // names are empty and the offsets 0 (nothing is written to the EEPROM).
    bool load();

    // Write to EEPROM. Only bytes that differ are written. Returns false if the
    // read-back does not match.
    bool save();

    const char* getName(uint8_t slot) const;
    // Returns false if slot or name is invalid (see isValidName).
    bool setName(uint8_t slot, const char* name);

    int16_t getFocusOffset(uint8_t slot) const;
    bool setFocusOffset(uint8_t slot, int16_t offset);

    uint8_t getStallThreshold() const { return data.stallThreshold; }
    void setStallThreshold(uint8_t value) { data.stallThreshold = value; }

    uint8_t getLedBrightness() const { return data.ledBrightness; }
    void setLedBrightness(uint8_t percent) { data.ledBrightness = percent > 100 ? 100 : percent; }

    // 0..15 printable ASCII characters, none of , # : |  (they are protocol / storage separators)
    static bool isValidName(const char* name);

private:
    ExternalEEPROM* eeprom;
    FilterConfigData data;

    void setDefaults();
    static uint16_t calculateCRC(const uint8_t* p, size_t length);
};
