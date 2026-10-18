// myFilterWheel Protocol Handler, version 2
// Line protocol: ":NN[params]#". Every command is answered:
//   queries      -> "<value>#"
//   set/actions  -> "OK#"
//   rejected     -> "ERR,<code>#"   (codes in error_codes.h)
#pragma once

#include <Arduino.h>

#include "config.h"

#include "error_codes.h"

// Forward declaration
class FilterWheel;

class ProtocolMyFW {
public:
    explicit ProtocolMyFW(FilterWheel* fw);

    // Process incoming serial data
    void process();

private:
    FilterWheel* filterWheel;

    // Command buffer
    char cmdBuffer[48];  // Buffer for command and parameters
    uint8_t bufferIndex;
    bool inCommand;      // True when between : and #
    bool overflow;       // Line did not fit in cmdBuffer

    // Handle a complete command
    void handleCommand();

    // Helper methods
    void sendResponse(const char* msg);
    void sendResponse(int value);
    void sendOk();
    void sendError(FwError code);
    void sendResult(FwError code);   // OK# or ERR,code#
};
