// Stage 1 of the wheel detection: find USB serial ports with the OpenWheel VID/PID in sysfs.
// Stage 2 (the :25# / :32# handshake) is Wheel::open().
#pragma once

#include <string>
#include <vector>

namespace openwheel
{

constexpr int UsbVid = 0x1209;          // pid.codes
constexpr int UsbPidOpenWheel = 0x2180; // registered (pull request pending, see docs/USB_IDS.md)
constexpr int UsbPidTest = 0x0001;      // shared pid.codes test PID used by development firmware

struct UsbPort
{
    std::string device;   // /dev/ttyACM0
    std::string byId;     // /dev/serial/by-id/... when present, else empty
    int vid = 0, pid = 0;
    std::string serial;   // USB serial number (the MCU UID)
};

bool isCandidate(const UsbPort &p);

/// Lists OpenWheel candidates: VID 0x1209 with PID 0x2180 or 0x0001. Order: the preferred USB
/// serial first, then the registered PID before the test PID, then by device name.
/// `sysClassTty` and `devRoot` are parameters so tests can point them at a fake tree.
std::vector<UsbPort> findCandidates(const std::string &preferredSerial = std::string(),
                                    const std::string &sysClassTty = "/sys/class/tty",
                                    const std::string &devRoot = "/dev");

}
