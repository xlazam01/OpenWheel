// The wheel session behind the INDI driver: owns the serial link, sends one short command at a
// time with a bounded timeout, polls the status, guards moves with a watchdog and caches the
// filter names, focus offsets and housekeeping stored in the wheel. No libindi dependency, so it
// is tested directly against the simulator. Behaviour follows driver/ASCOM/OpenWheel.Core.
#pragma once

#include "protocol.h"
#include "serial_link.h"

#include <functional>
#include <string>
#include <vector>

namespace openwheel
{

struct WheelOptions
{
    int commandTimeoutMs = 500;
    int eepromTimeoutMs = 2000;      // :37 / :39 write the EEPROM (~0.25 s)
    int pollIdleMs = 1000;
    int pollBusyMs = 200;
    int housekeepingMs = 5000;
    int maxConsecutiveFailures = 3;  // timeouts / garbage in a row before the connection is lost
    int moveTimeoutMs = 45000;       // the firmware gives up after 30 s; the driver halts after this
    int calibrationTimeoutMs = 120000;
};

struct Result
{
    bool ok = true;
    int code = 0;          // firmware error code when the wheel rejected the command
    std::string message;
    static Result success() { return Result(); }
    static Result fail(const std::string &m, int c = 0) { Result r; r.ok = false; r.code = c; r.message = m; return r; }
};

class Wheel
{
    public:
        explicit Wheel(WheelOptions o = WheelOptions()) : opt(o) {}

        /// Opens the port (or uses `fd` when >= 0, e.g. INDI's serial connection), runs the
        /// :25# / :32# handshake, reads the status, names, offsets and housekeeping.
        bool open(const std::string &port, std::string &why, int fd = -1);
        void close();

        bool connected() const { return isConnected; }
        /// Why the connection was lost or refused, or the current device problem; empty if none.
        const std::string &problem() const { return problemText; }

        /// Call regularly (every 50..200 ms). Polls when due, runs the watchdog, refreshes names
        /// and housekeeping. Returns true when anything visible changed. May end the connection.
        bool service();
        /// Milliseconds until the next poll is due (for the INDI timer).
        int nextServiceInMs() const;

        const Identity &identity() const { return id; }
        const Status &status() const { return st; }
        const std::string &portName() const { return link.name(); }
        bool configSupported() const { return cfgSupported; }
        bool configValid() const { return !names.empty(); }
        const std::vector<std::string> &filterNames() const { return names; }
        const std::vector<int> &focusOffsets() const { return offsets; }
        bool housekeepingSupported() const { return hkSupported; }
        bool housekeepingValid() const { return hkValid; }
        const Housekeeping &housekeeping() const { return hk; }
        bool watchdogTripped() const { return watchdogHit; }

        Result moveTo(int slot);          // wheel slot, 0-based
        Result home();
        Result calibrate();
        Result halt();
        Result clearError();
        /// Writes names and focus offsets to the wheel; only changed entries are sent, then read back.
        Result saveFilterConfig(const std::vector<std::string> &names, const std::vector<int> &offsets);

        std::function<void(const std::string &)> log;

    private:
        Reply send(const std::string &command, int timeoutMs = 0);
        void registerFailure(const std::string &what);
        void lose(const std::string &why);
        bool poll();
        void watchdog();
        bool readFilterConfig(int slots);
        bool readHousekeeping();
        Result action(const std::string &command);
        Result rejected(const Reply &r) const;
        std::string deviceProblem() const;
        void say(const std::string &m) { if (log) log(m); }

        WheelOptions opt;
        SerialLink link;
        bool isConnected = false;
        std::string problemText, stickyProblem;
        int failures = 0;

        Identity id;
        Status st;
        long long nextPollMs = 0;
        long long busySinceMs = -1;
        bool watchdogHit = false;

        bool cfgSupported = true;
        int cfgSlots = -1;
        long long cfgRetryMs = 0;
        std::vector<std::string> names;
        std::vector<int> offsets;

        bool hkSupported = true, hkValid = false;
        long long hkNextMs = 0;
        Housekeeping hk;
};

long long monotonicMs();

}
