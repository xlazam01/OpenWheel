#include "wheel.h"

#include <algorithm>
#include <chrono>

namespace openwheel
{

long long monotonicMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// The only way into a port: nothing but :25# and :32# is sent before the identify succeeded (the
// port may belong to another device on the shared test PID 1209:0001, and protocol 3 firmware
// answers everything else with ERR,16 until it has been identified).
bool Wheel::open(const std::string &port, std::string &why, int fd)
{
    close();
    if (fd >= 0) link.attach(fd, port);
    else if (!link.open(port, why)) return false;

    // The wheel may still be booting (USB enumerates before the firmware loop runs): retry the ping.
    bool alive = false;
    for (int i = 0; i < 3 && !alive && !link.broken(); i++)
        alive = link.execute(cmd::Ping, opt.commandTimeoutMs).isOk();
    if (!alive) { why = port + ": no answer to ping"; link.close(); return false; }

    Reply r = link.execute(cmd::Identify, opt.commandTimeoutMs);
    if (r.kind == Reply::Error && r.code == ErrUnknownCommand) { why = port + ": firmware too old (no identify command)"; link.close(); return false; }
    if (r.kind != Reply::Value || !Identity::parse(r.value, id)) { why = port + ": not an OpenWheel (identify answered " + r.toString() + ")"; link.close(); return false; }
    if (!id.supported())
    {
        why = port + ": protocol version " + std::to_string(id.protocol) + " is not supported (need >= " + std::to_string(MinProtocolVersion) + ")";
        link.close();
        return false;
    }

    Reply s = link.execute(cmd::Status, opt.commandTimeoutMs);
    if (s.kind != Reply::Value || !Status::parse(s.value, st)) { why = port + ": no valid status after identify"; link.close(); return false; }

    isConnected = true;
    failures = 0;
    problemText.clear();
    stickyProblem.clear();
    busySinceMs = -1;
    watchdogHit = false;
    cfgSupported = true;
    cfgSlots = -1;
    cfgRetryMs = 0;
    names.clear();
    offsets.clear();
    hkSupported = true;
    hkValid = false;

    readFilterConfig(st.slots);
    if (!isConnected) { why = problemText; return false; }
    readHousekeeping();
    if (!isConnected) { why = problemText; return false; }
    nextPollMs = monotonicMs() + (st.busy() ? opt.pollBusyMs : opt.pollIdleMs);
    problemText = deviceProblem();
    say("connected " + port + ": " + id.toString());
    return true;
}

void Wheel::close()
{
    link.close();
    isConnected = false;
}

int Wheel::nextServiceInMs() const
{
    long long left = nextPollMs - monotonicMs();
    return static_cast<int>(std::max(0LL, std::min<long long>(left, opt.pollIdleMs)));
}

// ------------------------------------------------------------------ transport with failure accounting

Reply Wheel::send(const std::string &command, int timeoutMs)
{
    if (!isConnected) return Reply::timeout();
    int t = timeoutMs > 0 ? timeoutMs : opt.commandTimeoutMs;
    Reply r = link.execute(command, t);
    if (!link.broken() && r.kind == Reply::Error && r.code == ErrNotIdentified)
    {
        // The wheel locked itself again (it saw the port being reconfigured): identify it once
        // more, check it is still the same wheel, then repeat the command once.
        say("wheel answered 'not identified': identifying again");
        Reply i = link.execute(cmd::Identify, opt.commandTimeoutMs);
        Identity again;
        if (link.broken()) { lose("the wheel was disconnected (USB unplugged?)"); return Reply::timeout(); }
        if (i.kind != Reply::Value || !Identity::parse(i.value, again))
        {
            lose("connection lost: wheel locked and did not identify again (" + i.toString() + ")");
            return Reply::timeout();
        }
        if (again.serial != id.serial)
        {
            lose("connection lost: a different device answered (serial " + again.serial + ")");
            return Reply::timeout();
        }
        r = link.execute(command, t);
    }
    if (link.broken()) { lose("the wheel was disconnected (USB unplugged?)"); return Reply::timeout(); }
    if (r.isTransportFailure())
        registerFailure(r.kind == Reply::Timeout ? "wheel did not answer " + command : "wheel sent garbage to " + command);
    else
        failures = 0;
    return r;
}

void Wheel::registerFailure(const std::string &what)
{
    failures++;
    say(what + " (" + std::to_string(failures) + "/" + std::to_string(opt.maxConsecutiveFailures) + ")");
    if (failures >= opt.maxConsecutiveFailures) lose("connection lost: " + what);
}

void Wheel::lose(const std::string &why)
{
    if (!isConnected) return;
    say(why);
    problemText = why;
    link.close();
    isConnected = false;
}

// ------------------------------------------------------------------ polling

bool Wheel::service()
{
    if (!isConnected) return false;
    if (monotonicMs() < nextPollMs) return false;

    Status before = st;
    std::string probBefore = problemText;
    bool cfgBefore = configValid();
    bool changed = poll();
    if (!isConnected) return true;
    watchdog();
    if (!isConnected) return true;

    // filter config: missing, or the wheel was recalibrated to another slot count
    if (cfgSupported && !st.busy() && st.slots > 0 && (names.empty() || cfgSlots != st.slots) && monotonicMs() >= cfgRetryMs)
        readFilterConfig(st.slots);
    if (!isConnected) return true;

    if (hkSupported)
    {
        bool flagChanged = hkValid && st.hasWarning() != (hk.warnings != 0);
        if (flagChanged || monotonicMs() >= hkNextMs)
            changed |= readHousekeeping();   // housekeeping numbers are shown live
    }
    if (!isConnected) return true;

    nextPollMs = monotonicMs() + (st.busy() ? opt.pollBusyMs : opt.pollIdleMs);
    return changed || problemText != probBefore || cfgBefore != configValid() ||
           before.state != st.state || before.slot != st.slot;
}

bool Wheel::poll()
{
    Reply r = send(cmd::Status);
    if (!isConnected) return true;
    Status s;
    if (r.kind == Reply::Value && Status::parse(r.value, s))
    {
        bool changed = s.state != st.state || s.slot != st.slot || s.target != st.target || s.slots != st.slots ||
                       s.error != st.error || s.flags != st.flags;
        if (s.error != 0 && s.error != st.error) say("wheel fault: " + describeError(s.error));
        st = s;
        problemText = deviceProblem();
        return changed;
    }
    if (r.kind == Reply::Value) { registerFailure("unexpected reply to status: " + r.value); return true; }
    if (r.kind == Reply::Error) { problemText = "wheel rejected the status request: " + describeError(r.code); return true; }
    return false;   // timeout/garbage: counted in send()
}

void Wheel::watchdog()
{
    if (!st.busy() || st.state == WheelState::Init) { busySinceMs = -1; watchdogHit = false; return; }
    long long now = monotonicMs();
    if (busySinceMs < 0) busySinceMs = now;
    int limit = st.state == WheelState::Calibrating ? opt.calibrationTimeoutMs : opt.moveTimeoutMs;
    if (now - busySinceMs > limit && !watchdogHit)
    {
        watchdogHit = true;
        say("watchdog: wheel busy for more than " + std::to_string(limit) + " ms, halting");
        send(cmd::Halt);
        stickyProblem = "wheel did not finish in time and was halted by the driver";
        problemText = stickyProblem;
        nextPollMs = 0;
    }
}

std::string Wheel::deviceProblem() const
{
    if (st.error != 0) return "wheel fault " + std::to_string(st.error) + ": " + describeError(st.error);
    if (st.eepromFault()) return "wheel reports an EEPROM fault";
    if (st.slots == 0 && st.state != WheelState::Calibrating && st.state != WheelState::Init) return "wheel is not calibrated";
    return stickyProblem;
}

// ------------------------------------------------------------------ names, offsets, housekeeping

bool Wheel::readFilterConfig(int slots)
{
    if (slots <= 0) { names.clear(); offsets.clear(); cfgSlots = -1; return false; }
    Reply n = send(cmd::GetNames);
    if (!isConnected) return false;
    if (n.kind == Reply::Error && n.code == ErrUnknownCommand)
    {
        cfgSupported = false;
        names.clear();
        offsets.clear();
        say("firmware has no filter name storage");
        return false;
    }
    if (n.kind != Reply::Value) { cfgRetryMs = monotonicMs() + 3000; return false; }
    Reply o = send(cmd::GetOffsets);
    if (!isConnected) return false;
    std::vector<int> off;
    if (o.kind != Reply::Value || !parseOffsets(o.value, slots, off)) { cfgRetryMs = monotonicMs() + 3000; return false; }
    names = parseNames(n.value, slots);
    offsets = off;
    cfgSlots = slots;
    return true;
}

bool Wheel::readHousekeeping()
{
    hkNextMs = monotonicMs() + opt.housekeepingMs;
    Reply r = send(cmd::Housekeeping);
    if (!isConnected) return false;
    if (r.kind == Reply::Error && r.code == ErrUnknownCommand)
    {
        hkSupported = false;
        say("firmware has no housekeeping command");
        return false;
    }
    Housekeeping h;
    if (r.kind != Reply::Value || !Housekeeping::parse(r.value, h)) return false;
    int before = hkValid ? hk.warnings : 0;
    if (h.warnings != before)
    {
        int added = h.warnings & ~before;
        if (added) say("wheel warning: " + Housekeeping::describeWarnings(added));
        else say("wheel warnings cleared, now: " + Housekeeping::describeWarnings(h.warnings));
    }
    hk = h;
    hkValid = true;
    return true;
}

// ------------------------------------------------------------------ commands

Result Wheel::rejected(const Reply &r) const
{
    return Result::fail("wheel rejected the command: " + describeError(r.code), r.code);
}

Result Wheel::action(const std::string &command)
{
    if (!isConnected) return Result::fail("not connected");
    Reply r = send(command);
    if (!isConnected) return Result::fail("connection lost");
    if (r.isOk())
    {
        stickyProblem.clear();
        // Refresh at once: a client that looks right after an accepted move must see "busy".
        poll();
        nextPollMs = monotonicMs() + opt.pollBusyMs;
        return Result::success();
    }
    if (r.kind == Reply::Error) return rejected(r);
    return Result::fail("no valid answer from the wheel to " + command);
}

Result Wheel::moveTo(int slot)
{
    if (!isConnected) return Result::fail("not connected");
    if (st.slots <= 0) return Result::fail("wheel is not calibrated", ErrNotCalibrated);
    if (slot < 0 || slot >= st.slots)
        return Result::fail("slot " + std::to_string(slot) + " is out of range (0.." + std::to_string(st.slots - 1) + ")", ErrBadParam);
    if (st.state == WheelState::Idle && st.positionKnown() && st.slot == slot) return Result::success();

    Reply r = send(cmd::moveToSlot(slot));
    if (!isConnected) return Result::fail("connection lost");
    if (r.kind == Reply::Error) return rejected(r);
    if (r.isOk())
    {
        stickyProblem.clear();
        poll();
        nextPollMs = monotonicMs() + opt.pollBusyMs;
        return Result::success();
    }
    // Reply lost or garbled: ask the wheel whether the move actually started.
    Reply s = send(cmd::Status);
    Status s2;
    if (s.kind == Reply::Value && Status::parse(s.value, s2) && s2.state == WheelState::Moving && s2.target == slot)
    {
        st = s2;
        nextPollMs = monotonicMs() + opt.pollBusyMs;
        return Result::success();
    }
    return Result::fail("no valid answer from the wheel");
}

Result Wheel::home() { return action(cmd::Home); }
Result Wheel::calibrate() { return action(cmd::Calibrate); }
Result Wheel::halt() { return action(cmd::Halt); }

Result Wheel::clearError()
{
    Result r = action(cmd::ClearError);
    if (r.ok) { stickyProblem.clear(); problemText = deviceProblem(); }
    return r;
}

Result Wheel::saveFilterConfig(const std::vector<std::string> &newNames, const std::vector<int> &newOffsets)
{
    if (!isConnected) return Result::fail("not connected");
    if (newNames.size() != newOffsets.size() || newNames.size() > static_cast<size_t>(MaxSlots))
        return Result::fail("invalid filter configuration");
    for (const auto &n : newNames)
        if (!isValidName(n)) return Result::fail("invalid filter name \"" + n + "\" (max 15 characters, none of , # : |)");
    for (int o : newOffsets)
        if (o < -32768 || o > 32767) return Result::fail("focus offset " + std::to_string(o) + " is out of range (-32768..32767)");
    if (st.busy()) return Result::fail("the wheel is moving: wait until it has stopped", ErrBusy);
    if (!cfgSupported) return Result::fail("this firmware cannot store filter names (update the firmware)");

    for (size_t i = 0; i < newNames.size(); i++)
    {
        int slot = static_cast<int>(i);
        if (i >= names.size() || names[i] != newNames[i])
        {
            Reply r = send(cmd::setName(slot, newNames[i]), opt.eepromTimeoutMs);
            if (!isConnected) return Result::fail("connection lost");
            if (!r.isOk()) return r.kind == Reply::Error ? rejected(r) : Result::fail("no valid answer from the wheel while saving name " + std::to_string(slot + 1));
        }
        if (i >= offsets.size() || offsets[i] != newOffsets[i])
        {
            Reply r = send(cmd::setFocusOffset(slot, newOffsets[i]), opt.eepromTimeoutMs);
            if (!isConnected) return Result::fail("connection lost");
            if (!r.isOk()) return r.kind == Reply::Error ? rejected(r) : Result::fail("no valid answer from the wheel while saving offset " + std::to_string(slot + 1));
        }
    }
    if (!readFilterConfig(st.slots)) return Result::fail("saved, but reading the values back failed");
    return Result::success();
}

}
