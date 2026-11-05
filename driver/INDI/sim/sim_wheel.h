// Simulated OpenWheel: a model of the firmware's protocol and state machine (docs/PROTOCOL.md),
// served on a pseudo terminal so the driver talks to it exactly like to /dev/ttyACM0.
// Fault injection covers what the ASCOM fake device covers: silence, garbage, lost '#',
// slow replies, sticky faults, stalls, unplug.
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace openwheel
{

struct SimConfig
{
    int slots = 5;                 // slots found by calibration (0 = not calibrated)
    int msPerSlot = 300;           // move time per slot
    int calibrationMs = 2000;
    int errorHoldMs = 10000;       // the firmware stays in the error state for 10 s
    int eepromMs = 250;            // :37 / :39 answer after this
    int initMs = 0;                // time in the init state after start
    bool filterConfig = true;      // false: :36..:39 answer ERR,10 (old firmware)
    bool housekeeping = true;      // false: :40 answers ERR,10
    std::string serial = "3A00FF12ABCD";
    std::string firmware = "FW1.2.0";
    int protocol = 2;
};

class SimWheel
{
    public:
        explicit SimWheel(const SimConfig &c = SimConfig());

        /// Handles one command body (the text between ':' and '#'); returns the reply with '#'.
        std::string handle(const std::string &body, long long nowMs);
        /// Advances moves, homing, calibration and the error hold.
        void tick(long long nowMs);

        // --- inspection / fault injection (used by tests and the control channel)
        int state() const { return st; }
        int slot() const { return curSlot; }
        int error() const { return err; }
        bool positionKnown() const { return posKnown; }
        const std::vector<std::string> &names() const { return nameTab; }
        const std::vector<int> &offsets() const { return offTab; }
        int moves() const { return moveCount; }
        void setSlotCount(int n);
        /// Puts the wheel into the error state with a sticky fault, as a failed move would.
        void injectFault(int code, long long nowMs);
        /// The next move/home ends with this fault instead of arriving (e.g. 6 no hall, 13 stalled).
        void failNextMove(int code) { pendingFault = code; }
        /// Moves never finish (motor turns forever): for the driver's watchdog.
        void setStuck(bool s) { stuck = s; }
        void setWarnings(int w) { warnings = w; }

        SimConfig cfg;

    private:
        void startMotion(int newState, int target, int durationMs, long long now);
        std::string status() const;

        int st;                 // 0 init, 1 idle, 2 moving, 3 homing, 4 calibrating, 5 error
        int curSlot = 0, target = -1, slotCount;
        bool posKnown = false;
        int err = 0, warnings = 0;
        long long until = 0;    // end of the current move / calibration / error hold
        long long started = -1;
        int pendingFault = 0;
        bool stuck = false;
        int moveCount = 0;
        std::vector<std::string> nameTab;
        std::vector<int> offTab;
};

/// Serves a SimWheel on a pty. The driver opens `path()`.
class SimServer
{
    public:
        explicit SimServer(const SimConfig &c = SimConfig());
        ~SimServer();

        bool start(std::string &why);
        void stop();
        /// Slave side of the pty (/dev/pts/N).
        const std::string &path() const { return slavePath; }

        /// Runs `f` on the model under the server lock (safe while the server thread runs).
        void with(const std::function<void(SimWheel &)> &f);

        // Link faults
        std::atomic<bool> silent{false};     // read commands, never answer
        std::atomic<bool> garbage{false};    // answer binary junk with a '#'
        std::atomic<bool> noHash{false};     // answer without the closing '#'
        std::atomic<int> delayMs{0};         // answer late
        std::atomic<int> received{0};        // commands seen
        /// Closes the master side: the driver sees EIO/HUP like a USB unplug.
        void unplug();

        /// Last commands seen (bodies), newest last; for tests.
        std::vector<std::string> history();

    private:
        void run();

        SimWheel wheel;
        std::mutex lock;
        std::vector<std::string> hist;
        int master = -1, slaveKeep = -1;
        std::string slavePath;
        std::thread worker;
        std::atomic<bool> running{false};
};

long long simNowMs();

}
