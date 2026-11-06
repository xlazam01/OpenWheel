#include "protocol.h"

#include <cstdlib>
#include <sstream>

namespace openwheel
{

namespace
{
std::vector<std::string> split(const std::string &s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(' ');
    return s.substr(a, b - a + 1);
}

bool parseInts(const std::vector<std::string> &p, size_t first, int *out, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (!parseInt(p[first + i], out[i])) return false;
    return true;
}
}

bool parseInt(const std::string &s, int &out)
{
    if (s.empty() || s.size() > 11) return false;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (i == s.size()) return false;
    long v = 0;
    for (; i < s.size(); i++)
    {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    out = static_cast<int>(s[0] == '-' ? -v : v);
    return true;
}

bool isFault(int code) { return (code >= 1 && code <= 6) || code == 13; }

std::string describeError(int code)
{
    switch (code)
    {
        case 0: return "no error";
        case 1: return "homing did not finish in time";
        case 2: return "move did not finish in time";
        case 3: return "calibration failed";
        case 4: return "EEPROM missing or write failed";
        case 5: return "motor driver (TMC2209) not answering or reporting overtemperature/short circuit";
        case 6: return "no hall sensor pulse seen while moving (sensor or magnets)";
        case 7: return "bad parameter";
        case 8: return "wheel is busy (moving, homing or calibrating)";
        case 9: return "wheel is not calibrated";
        case 10: return "unknown command (firmware too old?)";
        case 11: return "malformed command";
        case 12: return "setting cannot be changed at runtime";
        case 13: return "motor stalled (wheel blocked or jammed)";
        case 16: return "wheel is locked until the host identifies it (:32#)";
        default: return "unknown error code " + std::to_string(code);
    }
}

const char *stateName(WheelState s)
{
    switch (s)
    {
        case WheelState::Init: return "Initialising";
        case WheelState::Idle: return "Idle";
        case WheelState::Moving: return "Moving";
        case WheelState::Homing: return "Homing";
        case WheelState::Calibrating: return "Calibrating";
        case WheelState::Error: return "Error";
    }
    return "?";
}

Reply Reply::parse(const std::string &line)
{
    for (unsigned char c : line)
        if (c < 0x20 || c > 0x7E) return garbage();
    Reply r;
    if (line == "OK") { r.kind = Ok; return r; }
    if (line.compare(0, 4, "ERR,") == 0)
    {
        std::string n = line.substr(4);
        int code;
        if (!n.empty() && n[0] != '-' && parseInt(n, code) && code <= 255)
        {
            r.kind = Error;
            r.code = code;
            return r;
        }
        return garbage();
    }
    r.kind = Value;
    r.value = line;
    return r;
}

std::string Reply::toString() const
{
    switch (kind)
    {
        case Ok: return "OK";
        case Error: return "ERR," + std::to_string(code);
        case Value: return value;
        case Timeout: return "timeout";
        case Garbage: return "garbage";
    }
    return "?";
}

bool Identity::parse(const std::string &value, Identity &out)
{
    auto p = split(value, ',');
    if (p.size() != 5 || p[0] != "OPENWHEEL") return false;
    int proto;
    if (p[3].empty() || p[3][0] == '-' || !parseInt(p[3], proto)) return false;
    if (p[1].empty() || p[2].empty() || p[4].empty()) return false;
    out.model = p[1];
    out.firmware = p[2];
    out.protocol = proto;
    out.serial = p[4];
    return true;
}

std::string Identity::toString() const
{
    return "OpenWheel " + model + " " + firmware + " protocol " + std::to_string(protocol) + " serial " + serial;
}

bool Status::parse(const std::string &value, Status &out)
{
    auto p = split(value, ',');
    if (p.size() != 7 || p[0] != "S") return false;
    int n[6];
    if (!parseInts(p, 1, n, 6)) return false;
    if (n[0] < 0 || n[0] > 5) return false;
    if (n[1] < -1 || n[1] > 255 || n[2] < -1 || n[2] > 255) return false;
    if (n[3] < 0 || n[3] > 32) return false;
    if (n[4] < 0 || n[4] > 255 || n[5] < 0 || n[5] > 255) return false;
    out.state = static_cast<WheelState>(n[0]);
    out.slot = n[1];
    out.target = n[2];
    out.slots = n[3];
    out.error = n[4];
    out.flags = n[5];
    return true;
}

bool Housekeeping::parse(const std::string &value, Housekeeping &out)
{
    auto p = split(value, ',');
    if (p.size() != 7 || p[0] != "H") return false;
    int n[6];
    if (!parseInts(p, 1, n, 6)) return false;
    if (n[0] < -600 || n[0] > 1500 || n[1] < 0 || n[1] > 4000 || n[5] < 0 || n[5] > 255) return false;
    out.mcuTemp = n[0] / 10.0;
    out.vdda = n[1] / 1000.0;
    out.v5 = n[2] < 0 ? -1 : n[2] / 1000.0;
    out.v5min = n[3] < 0 ? -1 : n[3] / 1000.0;
    out.driverTemp = n[4] < 0 ? -1 : n[4];
    out.warnings = n[5];
    return true;
}

std::string Housekeeping::describeWarnings(int w)
{
    if (w == 0) return "none";
    std::string s;
    auto add = [&s](const char *t) { if (!s.empty()) s += "; "; s += t; };
    if (w & WarnSupplyLow) add("+5V supply below 4.75 V (weak USB port, hub or cable)");
    if (w & WarnDriverOvertemp) add("motor driver overtemperature prewarning");
    if (w & WarnDriverReset) add("motor driver was reset by a supply dip and re-initialised");
    if (w & WarnNoCrystal) add("crystal not running, clocks from the internal oscillator");
    return s;
}

bool isValidName(const std::string &name)
{
    if (name.size() > static_cast<size_t>(MaxNameLength)) return false;
    for (unsigned char c : name)
    {
        if (c < 0x20 || c > 0x7E) return false;
        if (c == ',' || c == '#' || c == ':' || c == '|') return false;
    }
    return true;
}

std::string sanitizeName(const std::string &name)
{
    std::string out;
    for (unsigned char c : trim(name))
    {
        if (c < 0x20 || c > 0x7E) continue;
        out += (c == ',' || c == '#' || c == ':' || c == '|') ? '-' : static_cast<char>(c);
        if (out.size() == static_cast<size_t>(MaxNameLength)) break;
    }
    return trim(out);
}

std::vector<std::string> parseNames(const std::string &value, int slots)
{
    auto p = split(value, ',');
    std::vector<std::string> out(slots > 0 ? slots : 0);
    for (int i = 0; i < slots; i++)
        out[i] = i < static_cast<int>(p.size()) ? trim(p[i]) : std::string();
    return out;
}

bool parseOffsets(const std::string &value, int slots, std::vector<int> &out)
{
    auto p = split(value, ',');
    std::vector<int> r(slots > 0 ? slots : 0, 0);
    for (int i = 0; i < slots && i < static_cast<int>(p.size()); i++)
    {
        int v;
        if (!parseInt(p[i], v) || v < -32768 || v > 32767) return false;
        r[i] = v;
    }
    out = r;
    return true;
}

namespace cmd
{
std::string moveToSlot(int slot) { return ":07 " + std::to_string(slot) + "#"; }
std::string setName(int slot, const std::string &name) { return ":37 " + std::to_string(slot) + "," + name + "#"; }
std::string setFocusOffset(int slot, int offset) { return ":39 " + std::to_string(slot) + "," + std::to_string(offset) + "#"; }
bool isValid(const std::string &c)
{
    return c.size() >= 4 && c.front() == ':' && c.back() == '#' &&
           static_cast<int>(c.size()) - 2 <= MaxCommandBody &&
           c.find('#') == c.size() - 1 && c.find(':', 1) == std::string::npos;
}
}

}
