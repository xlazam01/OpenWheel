// Error codes shared by firmware logic and the serial protocol.
// Sent to the host as "ERR,<code>#" (command rejected) or in the STATUS / error
// queries (sticky device fault). Keep in sync with driver/ASCOM/SPEC.md.
#pragma once
#include <stdint.h>

enum FwError : uint8_t {
    FW_OK                  = 0,   // no error
    // Sticky device faults (state ERROR, reported until cleared with :34,0#)
    FW_ERR_HOME_TIMEOUT    = 1,   // homing did not finish in time
    FW_ERR_MOVE_TIMEOUT    = 2,   // move did not finish in time
    FW_ERR_CALIB_FAILED    = 3,   // calibration did not complete
    FW_ERR_EEPROM          = 4,   // EEPROM missing, unreadable or write failed
    FW_ERR_MOTOR_DRIVER    = 5,   // TMC2209 not answering or reporting overtemp/short
    FW_ERR_HALL_NOT_SEEN   = 6,   // no hall sensor pulse seen while moving
    // Command rejections (returned only in the reply, do not change the device state)
    FW_ERR_BAD_PARAM       = 7,   // parameter missing, malformed or out of range
    FW_ERR_BUSY            = 8,   // device is moving / homing / calibrating
    FW_ERR_NOT_CALIBRATED  = 9,   // no valid calibration data
    FW_ERR_UNKNOWN_CMD     = 10,  // command number not implemented
    FW_ERR_BAD_FORMAT      = 11,  // malformed or too long command line
    FW_ERR_NOT_SUPPORTED   = 12   // command known but not changeable at runtime
};
