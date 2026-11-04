// OpenWheel serial protocol (version 2): reply classification, parsers and command builders.
// Pure code without I/O or libindi, shared by the INDI driver, the simulator and the tests.
// Reference: docs/PROTOCOL.md and driver/ASCOM/OpenWheel.Core/Protocol.cs.
#pragma once

#include <string>
#include <vector>

namespace openwheel
{

constexpr int MaxCommandBody = 46;   // characters between ':' and '#'
constexpr int MaxLineLength  = 64;   // longest plausible reply line
constexpr int MaxNameLength  = 15;
constexpr int MaxSlots       = 9;
constexpr int MinProtocolVersion = 2;

enum class WheelState { Init = 0, Idle = 1, Moving = 2, Homing = 3, Calibrating = 4, Error = 5 };

enum ErrorCode
{
    ErrNone = 0, ErrHomeTimeout = 1, ErrMoveTimeout = 2, ErrCalibration = 3, ErrEeprom = 4,
    ErrDriver = 5, ErrNoHall = 6, ErrBadParam = 7, ErrBusy = 8, ErrNotCalibrated = 9,
    ErrUnknownCommand = 10, ErrBadFormat = 11, ErrNotSupported = 12, ErrStalled = 13
};

/// True for sticky device faults (1..6 and 13), false for command rejections.
bool isFault(int code);
std::string describeError(int code);
const char *stateName(WheelState s);

struct Reply
{
    enum Kind { Value, Ok, Error, Timeout, Garbage };
    Kind kind = Timeout;
    std::string value;   // reply text without '#', for Value
    int code = 0;        // error code, for Error

    bool isOk() const { return kind == Ok; }
    bool isTransportFailure() const { return kind == Timeout || kind == Garbage; }
    std::string toString() const;

    /// Classifies one line (without the '#').
    static Reply parse(const std::string &line);
    static Reply timeout() { return Reply{}; }
    static Reply garbage() { Reply r; r.kind = Garbage; return r; }
};

struct Identity
{
    std::string model, firmware, serial;
    int protocol = 0;
    bool supported() const { return protocol >= MinProtocolVersion; }
    std::string toString() const;
    static bool parse(const std::string &value, Identity &out);
};

struct Status
{
    WheelState state = WheelState::Init;
    int slot = -1, target = -1, slots = 0, error = 0, flags = 0;

    bool calibrated() const { return flags & 1; }
    bool positionKnown() const { return flags & 2; }
    bool buttonAtBoot() const { return flags & 4; }
    bool eepromFault() const { return flags & 8; }
    bool hasWarning() const { return flags & 16; }
    bool busy() const
    {
        return state == WheelState::Moving || state == WheelState::Homing ||
               state == WheelState::Calibrating || state == WheelState::Init;
    }
    /// ASCOM style position: slot, or -1 while busy / unknown.
    int position() const
    {
        if (state != WheelState::Idle && state != WheelState::Error) return -1;
        return positionKnown() ? slot : -1;
    }
    static bool parse(const std::string &value, Status &out);
};

enum Warning { WarnSupplyLow = 1, WarnDriverOvertemp = 2, WarnDriverReset = 4, WarnNoCrystal = 8 };

struct Housekeeping
{
    double mcuTemp = 0;        // degrees C
    double vdda = 0;           // V
    double v5 = -1;            // V, -1 = not available
    double v5min = -1;         // V, -1 = not available
    int driverTemp = -1;       // 0, 120, 143, 150, 157; -1 = driver not answering
    int warnings = 0;
    static bool parse(const std::string &value, Housekeeping &out);
    static std::string describeWarnings(int w);
};

/// 0..15 printable ASCII characters, none of , # : |  (same rule as the firmware).
bool isValidName(const std::string &name);
/// Trims, replaces forbidden characters by '-', drops non-printable ones, cuts to 15.
std::string sanitizeName(const std::string &name);
/// Splits "a,b,,d" into exactly `slots` entries (empty entries kept, padded/cut).
std::vector<std::string> parseNames(const std::string &value, int slots);
/// Parses "0,-35,120" into `slots` integers; false if an entry is not a signed 16-bit number.
bool parseOffsets(const std::string &value, int slots, std::vector<int> &out);

/// Strict integer parse (optional leading '-'), no trailing characters.
bool parseInt(const std::string &s, int &out);

namespace cmd
{
inline const char *Ping = ":25#";
inline const char *Identify = ":32#";
inline const char *Status = ":33#";
inline const char *GetError = ":34#";
inline const char *ClearError = ":34,0#";
inline const char *Halt = ":30#";
inline const char *Home = ":20#";
inline const char *Calibrate = ":35#";
inline const char *GetNames = ":36#";
inline const char *GetOffsets = ":38#";
inline const char *Housekeeping = ":40#";
std::string moveToSlot(int slot);
std::string setName(int slot, const std::string &name);
std::string setFocusOffset(int slot, int offset);
/// True when `c` is a well formed ":NN...#" command within the length limit.
bool isValid(const std::string &c);
}

}
