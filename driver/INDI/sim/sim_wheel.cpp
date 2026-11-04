#include "sim_wheel.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>

namespace openwheel
{

namespace
{
constexpr int MaxSlots = 9;
constexpr int MaxBody = 46;

bool parseNum(const std::string &s, size_t &i, long &v)
{
    size_t start = i;
    bool neg = false;
    if (i < s.size() && s[i] == '-') { neg = true; i++; }
    if (i >= s.size() || s[i] < '0' || s[i] > '9') { i = start; return false; }
    v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; if (v > 1000000) return false; }
    if (neg) v = -v;
    return true;
}

bool parseOne(const std::string &p, long &a)
{
    size_t i = 0;
    return parseNum(p, i, a) && i == p.size();
}

bool parseTwo(const std::string &p, long &a, long &b)
{
    size_t i = 0;
    if (!parseNum(p, i, a) || i >= p.size() || p[i] != ',') return false;
    i++;
    return parseNum(p, i, b) && i == p.size();
}

std::string errReply(int code) { return "ERR," + std::to_string(code) + "#"; }
const std::string OK = "OK#";
}

long long simNowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ===================================================================== model

SimWheel::SimWheel(const SimConfig &c) : cfg(c), slotCount(c.slots), nameTab(MaxSlots), offTab(MaxSlots, 0)
{
    st = c.initMs > 0 ? 0 : 1;
    if (c.initMs > 0) until = simNowMs() + c.initMs;
    posKnown = slotCount > 0 && c.initMs == 0;
}

void SimWheel::setSlotCount(int n)
{
    slotCount = n;
    if (n == 0) posKnown = false;
}

void SimWheel::injectFault(int code, long long now)
{
    err = code;
    st = 5;
    target = -1;
    until = now + cfg.errorHoldMs;
}

void SimWheel::startMotion(int newState, int tgt, int durationMs, long long now)
{
    st = newState;
    target = tgt;
    posKnown = false;
    started = now;
    until = now + (durationMs > 0 ? durationMs : 1);
    moveCount++;
}

void SimWheel::tick(long long now)
{
    if (st == 0 && now >= until) { st = 1; posKnown = slotCount > 0; curSlot = 0; return; }
    if (st == 5 && now >= until) { st = 1; return; }           // error hold over; code stays sticky
    if ((st == 2 || st == 3) && !stuck && now >= until)
    {
        if (pendingFault) { injectFault(pendingFault, now); pendingFault = 0; return; }
        curSlot = st == 3 ? 0 : target;
        posKnown = true;
        target = -1;
        st = 1;
        return;
    }
    if (st == 4 && now >= until)
    {
        if (pendingFault) { injectFault(pendingFault, now); pendingFault = 0; return; }
        slotCount = cfg.slots > 0 ? cfg.slots : 5;
        curSlot = 0;
        posKnown = true;
        st = 1;
    }
}

std::string SimWheel::status() const
{
    bool busy = st == 0 || st == 2 || st == 3 || st == 4;
    int slot = (busy || !posKnown) ? -1 : curSlot;
    int flags = (slotCount > 0 ? 1 : 0) | (posKnown && !busy ? 2 : 0) | (warnings ? 16 : 0);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "S,%d,%d,%d,%d,%d,%d#", st, slot, st == 2 ? target : -1, slotCount, err, flags);
    return buf;
}

std::string SimWheel::handle(const std::string &body, long long now)
{
    tick(now);
    if (body.size() > static_cast<size_t>(MaxBody)) return errReply(11);
    if (body.size() < 2 || body[0] < '0' || body[0] > '9' || body[1] < '0' || body[1] > '9') return errReply(11);
    int c = (body[0] - '0') * 10 + (body[1] - '0');
    std::string p = body.substr(2);
    if (!p.empty() && (p[0] == ' ' || p[0] == ',')) p = p.substr(1);
    long a = 0, b = 0;
    bool idle = st == 1;

    switch (c)
    {
        case 6: return std::to_string((st == 1 || st == 5) && posKnown ? curSlot : -1) + "#";
        case 7:
            if (!parseOne(p, a) || a < 0 || a > 255) return errReply(7);
            if (!idle) return errReply(8);
            if (slotCount <= 0) return errReply(9);
            if (a >= slotCount) return errReply(7);
            {
                int from = posKnown ? curSlot : 0;
                int dist = static_cast<int>((a - from + slotCount) % slotCount);
                startMotion(2, static_cast<int>(a), (dist == 0 ? slotCount : dist) * cfg.msPerSlot, now);
            }
            return OK;
        case 16: return std::to_string(slotCount) + "#";
        case 17: return std::string(st == 2 || st == 3 || st == 4 ? "1" : "0") + "#";
        case 18: return cfg.firmware + "#";
        case 19: return "OpenWheel#";
        case 20:
            if (!idle) return errReply(8);
            if (slotCount <= 0) return errReply(9);
            startMotion(3, -1, (slotCount / 2 + 1) * cfg.msPerSlot, now);
            return OK;
        case 25: return OK;
        case 30:
            if (st == 2 || st == 3 || st == 4) { st = 1; target = -1; posKnown = false; }
            return OK;
        case 32: return "OPENWHEEL,G0B1," + cfg.firmware + "," + std::to_string(cfg.protocol) + "," + cfg.serial + "#";
        case 33: return status();
        case 34:
            if (p.empty()) return "E," + std::to_string(err) + "#";
            if (parseOne(p, a) && a == 0) { err = 0; if (st == 5) st = 1; warnings &= ~4; return OK; }
            return errReply(7);
        case 35:
            if (!idle) return errReply(8);
            startMotion(4, -1, cfg.calibrationMs, now);
            return OK;
        case 36:
        case 38:
            if (!cfg.filterConfig) return errReply(10);
            {
                std::string out;
                for (int i = 0; i < slotCount && i < MaxSlots; i++)
                {
                    if (i) out += ',';
                    out += c == 36 ? nameTab[i] : std::to_string(offTab[i]);
                }
                return out + "#";
            }
        case 37:
        {
            if (!cfg.filterConfig) return errReply(10);
            size_t i = 0;
            if (!parseNum(p, i, a) || i >= p.size() || p[i] != ',' || a < 0 || a >= MaxSlots) return errReply(7);
            std::string n = p.substr(i + 1);
            size_t s = n.find_first_not_of(' ');
            n = s == std::string::npos ? std::string() : n.substr(s, n.find_last_not_of(' ') - s + 1);
            if (n.size() >= 16) return errReply(7);
            for (unsigned char ch : n)
                if (ch < 0x20 || ch > 0x7E || ch == ',' || ch == '#' || ch == ':' || ch == '|') return errReply(7);
            nameTab[a] = n;
            return OK;
        }
        case 39:
            if (!cfg.filterConfig) return errReply(10);
            if (!parseTwo(p, a, b) || a < 0 || a >= MaxSlots || b < -32768 || b > 32767) return errReply(7);
            offTab[a] = static_cast<int>(b);
            return OK;
        case 40:
            if (!cfg.housekeeping) return errReply(10);
            return "H,312,3297,5040,4890,0," + std::to_string(warnings) + "#";
        default:
            return errReply(10);
    }
}

// ===================================================================== pty server

SimServer::SimServer(const SimConfig &c) : wheel(c) {}

SimServer::~SimServer() { stop(); }

bool SimServer::start(std::string &why)
{
    int s = -1;
    char name[128];
    if (openpty(&master, &s, name, nullptr, nullptr) != 0)
    {
        why = std::string("openpty: ") + std::strerror(errno);
        return false;
    }
    struct termios tio;
    if (tcgetattr(s, &tio) == 0) { cfmakeraw(&tio); tcsetattr(s, TCSANOW, &tio); }
    // Keep a slave descriptor open so the pty survives while the driver reconnects; closing it
    // in unplug() together with the master makes the driver see a hang-up.
    slaveKeep = s;
    slavePath = name;
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    running = true;
    worker = std::thread([this] { run(); });
    return true;
}

void SimServer::stop()
{
    running = false;
    if (worker.joinable()) worker.join();
    if (master >= 0) { ::close(master); master = -1; }
    if (slaveKeep >= 0) { ::close(slaveKeep); slaveKeep = -1; }
}

void SimServer::unplug()
{
    running = false;
    if (worker.joinable()) worker.join();
    if (master >= 0) { ::close(master); master = -1; }
    if (slaveKeep >= 0) { ::close(slaveKeep); slaveKeep = -1; }
}

void SimServer::with(const std::function<void(SimWheel &)> &f)
{
    std::lock_guard<std::mutex> g(lock);
    f(wheel);
}

std::vector<std::string> SimServer::history()
{
    std::lock_guard<std::mutex> g(lock);
    return hist;
}

void SimServer::run()
{
    std::string line;
    bool inCmd = false;
    while (running)
    {
        struct pollfd p = { master, POLLIN, 0 };
        int r = poll(&p, 1, 20);
        {
            std::lock_guard<std::mutex> g(lock);
            wheel.tick(simNowMs());
        }
        if (r <= 0 || !(p.revents & POLLIN)) continue;
        char buf[256];
        ssize_t n = ::read(master, buf, sizeof(buf));
        if (n <= 0) continue;
        for (ssize_t i = 0; i < n; i++)
        {
            char c = buf[i];
            if (c == ':') { inCmd = true; line.clear(); continue; }   // ':' restarts a command
            if (!inCmd) continue;                                    // bytes outside :...# are ignored
            if (c != '#') { if (line.size() < 200) line += c; continue; }
            inCmd = false;
            received++;
            std::string reply;
            {
                std::lock_guard<std::mutex> g(lock);
                hist.push_back(line);
                if (hist.size() > 200) hist.erase(hist.begin());
                reply = wheel.handle(line, simNowMs());
            }
            if (silent) continue;
            int d = delayMs;
            if (line.compare(0, 2, "37") == 0 || line.compare(0, 2, "39") == 0) d += wheel.cfg.eepromMs;
            if (d > 0) std::this_thread::sleep_for(std::chrono::milliseconds(d));
            if (garbage) reply = std::string("\x01\xfe\x7f junk \x02", 11) + "#";
            else if (noHash && !reply.empty()) reply.pop_back();
            ssize_t w = ::write(master, reply.data(), reply.size());
            (void)w;
        }
    }
}

}
