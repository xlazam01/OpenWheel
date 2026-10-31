// Filter names and focus offsets in EEPROM
#include "filter_config.h"

FilterConfig::FilterConfig(ExternalEEPROM* e) : eeprom(e) {
    setDefaults();
}

void FilterConfig::setDefaults() {
    memset(&data, 0, sizeof(data));
    data.version = 2;
    data.ledBrightness = 100;
}

uint16_t FilterConfig::calculateCRC(const uint8_t* p, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= ((uint16_t)p[i] << 8);
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

bool FilterConfig::load() {
    FilterConfigData tmp;
    uint8_t* p = (uint8_t*)&tmp;
    for (size_t i = 0; i < sizeof(tmp); i++) {
        p[i] = eeprom->read(FILTER_CONFIG_ADDR + i);
    }

    bool ok = false;
    if (tmp.magic == FILTER_CONFIG_MAGIC && tmp.version == 2) {
        ok = tmp.crc == calculateCRC(p, sizeof(tmp) - sizeof(tmp.crc));
        if (tmp.ledBrightness > 100) tmp.ledBrightness = 100;
    } else if (tmp.magic == FILTER_CONFIG_MAGIC && tmp.version == 1) {
        // version 1: CRC right after focusOffsets; migrate (saved as version 2 on the next save)
        uint16_t crc1 = p[FILTER_CONFIG_V1_SIZE - 2] | ((uint16_t)p[FILTER_CONFIG_V1_SIZE - 1] << 8);
        ok = crc1 == calculateCRC(p, FILTER_CONFIG_V1_SIZE - 2);
        tmp.ledBrightness = 100;
        memset(tmp.reserved, 0, sizeof(tmp.reserved));
        tmp.version = 2;
    }
    if (ok) {
        // names must be terminated: never trust stored data
        for (uint8_t s = 0; s < MAX_SLOTS; s++) {
            tmp.names[s][FILTER_NAME_LEN - 1] = '\0';
            if (!isValidName(tmp.names[s])) tmp.names[s][0] = '\0';
        }
        data = tmp;
        return true;
    }
    setDefaults();
    return false;
}

bool FilterConfig::save() {
    data.magic = FILTER_CONFIG_MAGIC;
    data.version = 2;
    data.crc = calculateCRC((const uint8_t*)&data, sizeof(data) - sizeof(data.crc));

    const uint8_t* want = (const uint8_t*)&data;
    for (size_t i = 0; i < sizeof(data); i++) {
        uint16_t addr = FILTER_CONFIG_ADDR + i;
        if (eeprom->read(addr) != want[i]) {       // write only what changed: fast and gentle on the EEPROM
            eeprom->write(addr, want[i]);
            delay(5);                              // same settling time as the calibration writes
        }
    }
    delay(10);

    for (size_t i = 0; i < sizeof(data); i++) {
        if (eeprom->read(FILTER_CONFIG_ADDR + i) != want[i]) return false;
    }
    return true;
}

const char* FilterConfig::getName(uint8_t slot) const {
    return slot < MAX_SLOTS ? data.names[slot] : "";
}

bool FilterConfig::isValidName(const char* name) {
    size_t n = strlen(name);
    if (n >= FILTER_NAME_LEN) return false;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (c < 0x20 || c > 0x7E) return false;
        if (c == ',' || c == '#' || c == ':' || c == '|') return false;
    }
    return true;
}

bool FilterConfig::setName(uint8_t slot, const char* name) {
    if (slot >= MAX_SLOTS || !isValidName(name)) return false;
    memset(data.names[slot], 0, FILTER_NAME_LEN);
    strncpy(data.names[slot], name, FILTER_NAME_LEN - 1);
    return true;
}

int16_t FilterConfig::getFocusOffset(uint8_t slot) const {
    return slot < MAX_SLOTS ? data.focusOffsets[slot] : 0;
}

bool FilterConfig::setFocusOffset(uint8_t slot, int16_t offset) {
    if (slot >= MAX_SLOTS) return false;
    data.focusOffsets[slot] = offset;
    return true;
}
