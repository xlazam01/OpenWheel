// OpenWheel Protocol Implementation, version 2 (every command is acknowledged)
#include "protocol_openwheel.h"
#include "filterwheel.h"

// protoWrite() sends a complete response as one atomic write.
// DTR gating is disabled at startup via CDC_enableDTR(false) in main.cpp,
// so PROTOCOL_SERIAL.write() works regardless of DTR state on USB CDC targets.
static void protoWrite(const uint8_t* buf, size_t len) {
    PROTOCOL_SERIAL.write(buf, len);
}

// ---- parameter parsing ----------------------------------------------------
// Accepts an optional leading space or comma ("07 3", "07,3", "073") and
// requires the whole parameter to be a number. Returns false on missing/garbage.
static bool parseNumber(const char*& s, long& out) {
    while (*s == ' ' || *s == ',') s++;
    char* end = nullptr;
    out = strtol(s, &end, 10);
    if (end == s) return false;
    s = end;
    return true;
}

static bool parseInt(const char* s, long& out) {
    if (!parseNumber(s, out)) return false;
    while (*s == ' ') s++;
    return *s == '\0';
}

static bool parsePair(const char* s, long& a, long& b) {
    if (!parseNumber(s, a)) return false;
    if (*s != ',') return false;
    if (!parseNumber(s, b)) return false;
    while (*s == ' ') s++;
    return *s == '\0';
}

// Device serial number from the MCU unique ID. Same value as the USB serial string descriptor
// (stm32duino USB core: UID0+UID2 as 8 hex digits, then the TOP 16 bits of UID1).
static void deviceSerial(char* out, size_t len) {
    uint32_t w0 = HAL_GetUIDw0() + HAL_GetUIDw2();
    uint32_t w1 = HAL_GetUIDw1();
    snprintf(out, len, "%08lX%04lX", (unsigned long)w0, (unsigned long)(w1 >> 16));
}

// ─────────────────────────────────────────────────────────────────────────────

ProtocolOpenWheel::ProtocolOpenWheel(FilterWheel* fw)
    : filterWheel(fw), bufferIndex(0), inCommand(false), overflow(false) {
    memset(cmdBuffer, 0, sizeof(cmdBuffer));
}

void ProtocolOpenWheel::process() {
    while (PROTOCOL_SERIAL.available() > 0) {
        char inChar = PROTOCOL_SERIAL.read();

        if (inChar == ':') {
            inCommand = true;
            overflow = false;
            bufferIndex = 0;
            memset(cmdBuffer, 0, sizeof(cmdBuffer));
        } else if (inChar == '#') {
            if (inCommand) {
                cmdBuffer[bufferIndex] = '\0';
                if (overflow) {
                    sendError(FW_ERR_BAD_FORMAT);
                } else {
                    handleCommand();
                }
                inCommand = false;
                bufferIndex = 0;
            }
        } else if (inCommand) {
            if (bufferIndex < sizeof(cmdBuffer) - 1) {
                cmdBuffer[bufferIndex++] = inChar;
            } else {
                overflow = true;
            }
        }
        // bytes outside ":...#" are noise and are ignored without a reply
    }
}

void ProtocolOpenWheel::handleCommand() {
    if (bufferIndex < 2 || !isDigit(cmdBuffer[0]) || !isDigit(cmdBuffer[1])) {
        sendError(FW_ERR_BAD_FORMAT);
        return;
    }

    const int cmdNum = (cmdBuffer[0] - '0') * 10 + (cmdBuffer[1] - '0');
    const char* params = &cmdBuffer[2];
    long a = 0, b = 0;

    switch (cmdNum) {
        case 0:  // Get index adjust (user offset of slot 0)
            sendResponse(filterWheel->calibration.getUserOffset(0));
            break;

        case 1:  // Set index adjust (user offset of slot 0)
            if (!parseInt(params, a) || a < -32768 || a > 32767) { sendError(FW_ERR_BAD_PARAM); break; }
            filterWheel->calibration.setUserOffset(0, (int16_t)a);
            sendResult(filterWheel->calibration.save() ? FW_OK : FW_ERR_EEPROM);
            break;

        case 2:  // Get motor speed
            sendResponse(STEPPER_SPEED);
            break;

        case 3:  // Set motor speed (compile-time only)
            sendError(FW_ERR_NOT_SUPPORTED);
            break;

        case 4:  // Get reverse direction flag
            sendResponse((ROTATION_DIRECTION == ROTATION_DIRECTION_CCW) ? 1 : 0);
            break;

        case 5:  // Set reverse direction (compile-time only)
            sendError(FW_ERR_NOT_SUPPORTED);
            break;

        case 6:  // Get current slot (-1 while moving / position unknown)
            sendResponse(filterWheel->getReportedSlot());
            break;

        case 7:  // Move to slot
            if (!parseInt(params, a) || a < 0 || a > 255) { sendError(FW_ERR_BAD_PARAM); break; }
            sendResult(filterWheel->moveToSlot((uint8_t)a));
            break;

        case 8:  // Get startup behavior
            sendResponse(filterWheel->calibration.getStartupBehavior());
            break;

        case 9:  // Set startup behavior (0=stay, 1=home)
            if (!parseInt(params, a) || (a != 0 && a != 1)) { sendError(FW_ERR_BAD_PARAM); break; }
            filterWheel->calibration.setStartupBehavior((uint8_t)a);
            sendResult(filterWheel->calibration.save() ? FW_OK : FW_ERR_EEPROM);
            break;

        case 10:  // Get optical offsets for all slots (comma-separated, no trailing comma)
            {
                char buf[128];
                int pos = 0;
                uint8_t n = filterWheel->calibration.getNumSlots();
                for (uint8_t i = 0; i < n; i++) {
                    if (i > 0 && pos < (int)sizeof(buf) - 2) buf[pos++] = ',';
                    pos += snprintf(&buf[pos], sizeof(buf) - pos - 2, "%d",
                                    filterWheel->calibration.getOpticalOffset(i));
                }
                if (pos < (int)sizeof(buf)) buf[pos++] = '#';
                protoWrite((uint8_t*)buf, pos);
            }
            break;

        case 11:  // Set optical offset for a slot [slot,offset]
            if (!parsePair(params, a, b) || a < 0 || a >= filterWheel->calibration.getNumSlots() ||
                b < -32768 || b > 32767) { sendError(FW_ERR_BAD_PARAM); break; }
            filterWheel->calibration.setOpticalOffset((uint8_t)a, (int16_t)b);
            sendResult(filterWheel->calibration.save() ? FW_OK : FW_ERR_EEPROM);
            break;

        case 12:  // Get motor steps per slot (slots 1..N, each value + trailing comma, then #)
            {
                char buf[128];
                int pos = 0;
                uint8_t n = filterWheel->calibration.getNumSlots();
                for (uint8_t i = 1; i < n; i++) {
                    pos += snprintf(&buf[pos], sizeof(buf) - pos - 2, "%d,",
                                    filterWheel->calibration.getPulseLength(i));
                }
                if (pos < (int)sizeof(buf)) buf[pos++] = '#';
                protoWrite((uint8_t*)buf, pos);
            }
            break;

        case 13:  // Set steps for a slot [slotnumber,steps]
            if (!parsePair(params, a, b) || a < 1 || a >= filterWheel->calibration.getNumSlots() ||
                b < 0 || b > 65535) { sendError(FW_ERR_BAD_PARAM); break; }
            filterWheel->calibration.setPulseLength((uint8_t)a, (uint16_t)b);
            sendResult(filterWheel->calibration.save() ? FW_OK : FW_ERR_EEPROM);
            break;

        case 14:  // Get maxSteps
            sendResponse((int)filterWheel->calibration.getMaxSteps());
            break;

        case 15:  // Set maxSteps
            if (!parseInt(params, a) || a <= 0) { sendError(FW_ERR_BAD_PARAM); break; }
            filterWheel->calibration.setMaxSteps((uint32_t)a);
            sendResult(filterWheel->calibration.save() ? FW_OK : FW_ERR_EEPROM);
            break;

        case 16:  // Get number of filter slots
            sendResponse(filterWheel->calibration.getNumSlots());
            break;

        case 17:  // Is filter wheel moving (1=moving, 0=idle)
            sendResponse(filterWheel->isMoving() ? 1 : 0);
            break;

        case 18:  // Get firmware version
            sendResponse(FIRMWARE_VERSION);
            break;

        case 19:  // Get firmware name
            sendResponse(FIRMWARE_NAME);
            break;

        case 20:  // Home filter wheel
            sendResult(filterWheel->goHome());
            break;

        case 21:  // Move by specified number of steps
            if (!parseInt(params, a)) { sendError(FW_ERR_BAD_PARAM); break; }
            sendResult(filterWheel->moveSteps((int32_t)a));
            break;

        case 22:  // Reboot
            if (filterWheel->isMoving()) { sendError(FW_ERR_BUSY); break; }
            sendOk();
            PROTOCOL_SERIAL.flush();
            delay(50);
            NVIC_SystemReset();
            break;

        case 23:  // Write settings to EEPROM
            sendResult(filterWheel->calibration.save() ? FW_OK : FW_ERR_EEPROM);
            break;

        case 24:  // Reset to defaults (wheel is recalibrated on next boot)
            if (filterWheel->isMoving()) { sendError(FW_ERR_BUSY); break; }
            filterWheel->calibration.clear();
            sendResult(filterWheel->calibration.save() ? FW_OK : FW_ERR_EEPROM);
            break;

        case 25:  // Ping
            sendResponse("OK");
            break;

        case 26:  // Get homing method (always steps-based)
            sendResponse(1);
            break;

        case 27:  // Set homing method (not applicable)
            sendError(FW_ERR_NOT_SUPPORTED);
            break;

        case 28:  // Get HOMEFOUND LDR value (mapped to baseOffset)
            sendResponse(filterWheel->calibration.getBaseOffset());
            break;

        case 29:  // Set HOMEFOUND LDR value (stored as baseOffset)
            if (!parseInt(params, a) || a < 0 || a > 32767) { sendError(FW_ERR_BAD_PARAM); break; }
            filterWheel->calibration.setBaseOffset((int16_t)a);
            sendResult(filterWheel->calibration.save() ? FW_OK : FW_ERR_EEPROM);
            break;

        case 30:  // Halt movement
            filterWheel->stopMovement();
            sendOk();
            break;

        case 31:  // Reboot to DFU bootloader
            if (filterWheel->isMoving()) { sendError(FW_ERR_BUSY); break; }
            sendOk();
            PROTOCOL_SERIAL.flush();
            filterWheel->rebootToDFU();
            break;

        case 32:  // IDENTIFY: OPENWHEEL,<model>,<firmware>,<protocol version>,<serial>
            {
                char serial[16];
                char buf[64];
                deviceSerial(serial, sizeof(serial));
                int len = snprintf(buf, sizeof(buf), "OPENWHEEL,%s,%s,%d,%s#",
                                   DEVICE_MODEL, FIRMWARE_VERSION, PROTOCOL_VERSION, serial);
                protoWrite((const uint8_t*)buf, (size_t)len);
            }
            break;

        case 33:  // STATUS: S,<state>,<slot>,<target>,<nslots>,<error>,<flags>
            {
                char buf[48];
                int len = snprintf(buf, sizeof(buf), "S,%d,%d,%d,%d,%d,%d#",
                                   (int)filterWheel->getState(),
                                   (int)filterWheel->getReportedSlot(),
                                   (int)filterWheel->getTargetSlot(),
                                   (int)filterWheel->calibration.getNumSlots(),
                                   (int)filterWheel->getError(),
                                   (int)filterWheel->getFlags());
                protoWrite((const uint8_t*)buf, (size_t)len);
            }
            break;

        case 34:  // ERROR: ":34#" reads "E,<code>#"; ":34,0#" clears the sticky error
            if (params[0] == '\0') {
                char buf[16];
                int len = snprintf(buf, sizeof(buf), "E,%d#", (int)filterWheel->getError());
                protoWrite((const uint8_t*)buf, (size_t)len);
            } else if (parseInt(params, a) && a == 0) {
                filterWheel->clearError();
                sendOk();
            } else {
                sendError(FW_ERR_BAD_PARAM);
            }
            break;

        case 35:  // Start calibration
            sendResult(filterWheel->calibrate());
            break;

        case 36:  // Get filter names of the calibrated slots: "name0,name1,...#" (empty name = not set)
            {
                char buf[160];
                int pos = 0;
                uint8_t n = filterWheel->calibration.getNumSlots();
                for (uint8_t i = 0; i < n && i < MAX_SLOTS; i++) {
                    if (i > 0) buf[pos++] = ',';
                    pos += snprintf(&buf[pos], sizeof(buf) - pos - 2, "%s", filterWheel->filterConfig.getName(i));
                }
                buf[pos++] = '#';
                protoWrite((const uint8_t*)buf, pos);
            }
            break;

        case 37:  // Set filter name: ":37 slot,name#" (name 0..15 printable chars, none of , # : |)
            {
                const char* q = params;
                if (!parseNumber(q, a) || *q != ',' || a < 0 || a >= MAX_SLOTS) { sendError(FW_ERR_BAD_PARAM); break; }
                q++;
                char name[FILTER_NAME_LEN + 1];
                while (*q == ' ') q++;
                size_t len = strlen(q);
                while (len > 0 && q[len - 1] == ' ') len--;
                if (len >= FILTER_NAME_LEN) { sendError(FW_ERR_BAD_PARAM); break; }
                memcpy(name, q, len);
                name[len] = '\0';
                if (!filterWheel->filterConfig.setName((uint8_t)a, name)) { sendError(FW_ERR_BAD_PARAM); break; }
                sendResult(filterWheel->filterConfig.save() ? FW_OK : FW_ERR_EEPROM);
            }
            break;

        case 38:  // Get focus offsets of the calibrated slots: "o0,o1,...#"
            {
                char buf[96];
                int pos = 0;
                uint8_t n = filterWheel->calibration.getNumSlots();
                for (uint8_t i = 0; i < n && i < MAX_SLOTS; i++) {
                    if (i > 0) buf[pos++] = ',';
                    pos += snprintf(&buf[pos], sizeof(buf) - pos - 2, "%d", filterWheel->filterConfig.getFocusOffset(i));
                }
                buf[pos++] = '#';
                protoWrite((const uint8_t*)buf, pos);
            }
            break;

        case 39:  // Set focus offset: ":39 slot,offset#"
            if (!parsePair(params, a, b) || a < 0 || a >= MAX_SLOTS || b < -32768 || b > 32767) { sendError(FW_ERR_BAD_PARAM); break; }
            filterWheel->filterConfig.setFocusOffset((uint8_t)a, (int16_t)b);
            sendResult(filterWheel->filterConfig.save() ? FW_OK : FW_ERR_EEPROM);
            break;

        default:
            sendError(FW_ERR_UNKNOWN_CMD);
            break;
    }
}

void ProtocolOpenWheel::sendResponse(const char* msg) {
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%s#", msg);
    protoWrite((const uint8_t*)buf, (size_t)len);
}

void ProtocolOpenWheel::sendResponse(int value) {
    char buf[20];
    int len = snprintf(buf, sizeof(buf), "%d#", value);
    protoWrite((const uint8_t*)buf, (size_t)len);
}

void ProtocolOpenWheel::sendOk() {
    sendResponse("OK");
}

void ProtocolOpenWheel::sendError(FwError code) {
    char buf[20];
    int len = snprintf(buf, sizeof(buf), "ERR,%d#", (int)code);
    protoWrite((const uint8_t*)buf, (size_t)len);
}

void ProtocolOpenWheel::sendResult(FwError code) {
    if (code == FW_OK) {
        sendOk();
    } else {
        sendError(code);
    }
}
