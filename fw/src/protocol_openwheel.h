// OpenWheel Protocol Handler, version 3
// Line protocol: ":NN[params]#". Every command is answered:
//   queries      -> "<value>#"
//   set/actions  -> "OK#"
//   rejected     -> "ERR,<code>#"   (codes in error_codes.h)
// Identify first: after power-on and whenever a host opens the USB port, only :25# (ping) and
// :32# (identify) are accepted; everything else answers ERR,16 until :32# has been sent.
#pragma once

#include <Arduino.h>

#include "config.h"

#include "error_codes.h"

// Forward declaration
class FilterWheel;

class ProtocolOpenWheel {
public:
    explicit ProtocolOpenWheel(FilterWheel* fw);

    // Process incoming serial data
    void process();

private:
    FilterWheel* filterWheel;

    // Command buffer
    char cmdBuffer[48];  // Buffer for command and parameters
    uint8_t bufferIndex;
    bool inCommand;      // True when between : and #
    bool overflow;       // Line did not fit in cmdBuffer
    bool identified;     // Host sent :32# since power-on / since it opened the port

    // Handle a complete command
    void handleCommand();

    // Helper methods
    void sendResponse(const char* msg);
    void sendResponse(int value);
    void sendOk();
    void sendError(FwError code);
    void sendResult(FwError code);   // OK# or ERR,code#
};
