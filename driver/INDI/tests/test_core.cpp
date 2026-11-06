// Core tests: protocol parsing, USB detection on a fake sysfs tree, and the wheel session against
// the simulator on a real pty (moves, names, faults, silence, garbage, slow replies, unplug).
#include "check.h"

#include "detect.h"
#include "protocol.h"
#include "serial_link.h"
#include "sim_wheel.h"
#include "wheel.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace openwheel;

namespace
{
WheelOptions fastOptions()
{
    WheelOptions o;
    o.commandTimeoutMs = 300;
    o.eepromTimeoutMs = 1500;
    o.pollIdleMs = 100;
    o.pollBusyMs = 30;
    o.housekeepingMs = 300;
    return o;
}

SimConfig fastSim()
{
    SimConfig c;
    c.msPerSlot = 100;
    c.calibrationMs = 300;
    c.errorHoldMs = 400;
    c.eepromMs = 20;
    return c;
}

template <typename F> bool waitFor(Wheel &w, F pred, int timeoutMs)
{
    long long end = monotonicMs() + timeoutMs;
    while (monotonicMs() < end)
    {
        w.service();
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

struct Rig
{
    SimServer sim;
    Wheel wheel;
    bool ok = false;
    explicit Rig(SimConfig c = fastSim(), WheelOptions o = fastOptions()) : sim(c), wheel(o)
    {
        std::string why;
        ok = sim.start(why);
        if (!ok) { std::printf("  sim: %s\n", why.c_str()); return; }
        ok = wheel.open(sim.path(), why);
        if (!ok) std::printf("  open: %s\n", why.c_str());
    }
};

void sh(const std::string &c) { if (std::system(c.c_str()) != 0) std::printf("  command failed: %s\n", c.c_str()); }
void writeFile(const std::string &p, const std::string &s) { std::ofstream(p) << s << "\n"; }
}

// ===================================================================== protocol

TEST(reply_classification)
{
    CHECK(Reply::parse("OK").kind == Reply::Ok);
    Reply e = Reply::parse("ERR,8");
    CHECK(e.kind == Reply::Error && e.code == 8);
    CHECK(Reply::parse("ERR,x").kind == Reply::Garbage);
    CHECK(Reply::parse("ERR,-1").kind == Reply::Garbage);
    CHECK(Reply::parse("ERR,999").kind == Reply::Garbage);
    CHECK(Reply::parse(std::string("S,\x01", 3)).kind == Reply::Garbage);
    Reply v = Reply::parse("3");
    CHECK(v.kind == Reply::Value && v.value == "3");
}

TEST(identity_parse)
{
    Identity id;
    CHECK(Identity::parse("OPENWHEEL,G0B1,FW1.0.1,2,3A00FF12ABCD", id));
    CHECK_EQ(id.model, std::string("G0B1"));
    CHECK_EQ(id.protocol, 2);
    CHECK(id.supported());
    CHECK(Identity::parse("OPENWHEEL,G0B1,FW0.9,1,X", id) && !id.supported());
    CHECK(!Identity::parse("MYFW,G0B1,FW1,2,X", id));
    CHECK(!Identity::parse("OPENWHEEL,G0B1,FW1,2", id));
    CHECK(!Identity::parse("OPENWHEEL,,FW1,2,X", id));
    CHECK(!Identity::parse("OPENWHEEL,G0B1,FW1,-2,X", id));
}

TEST(status_parse)
{
    Status s;
    CHECK(Status::parse("S,1,2,-1,5,0,3", s));
    CHECK(s.state == WheelState::Idle && s.slot == 2 && s.slots == 5 && s.calibrated() && s.positionKnown());
    CHECK_EQ(s.position(), 2);
    CHECK(Status::parse("S,2,-1,4,5,0,1", s));
    CHECK(s.busy());
    CHECK_EQ(s.position(), -1);
    CHECK_EQ(s.target, 4);
    CHECK(Status::parse("S,5,1,-1,5,6,3", s) && s.error == 6 && s.position() == 1);
    CHECK(!Status::parse("S,6,1,-1,5,0,3", s));     // unknown state
    CHECK(!Status::parse("S,1,1,-1,5,0", s));       // field missing
    CHECK(!Status::parse("S,1,1,-1,99,0,3", s));    // slot count
    CHECK(!Status::parse("X,1,1,-1,5,0,3", s));
    CHECK(!Status::parse("S,1,1a,-1,5,0,3", s));
}

TEST(housekeeping_parse)
{
    Housekeeping h;
    CHECK(Housekeeping::parse("H,312,3297,5040,4890,0,0", h));
    CHECK(h.mcuTemp > 31.1 && h.mcuTemp < 31.3);
    CHECK(h.v5 > 5.03 && h.v5 < 5.05);
    CHECK(Housekeeping::parse("H,312,3297,-1,-1,-1,5", h));
    CHECK(h.v5 < 0 && h.v5min < 0 && h.driverTemp == -1 && h.warnings == 5);
    CHECK(Housekeeping::describeWarnings(5).find("supply") != std::string::npos);
    CHECK(!Housekeeping::parse("H,312,3297,5040,4890,0", h));
    CHECK(!Housekeeping::parse("H,9999,3297,5040,4890,0,0", h));
}

TEST(names_and_offsets)
{
    CHECK(isValidName(""));
    CHECK(isValidName("L-eXtreme"));
    CHECK(isValidName("123456789012345"));
    CHECK(!isValidName("1234567890123456"));
    CHECK(!isValidName("a,b"));
    CHECK(!isValidName("a#"));
    CHECK(!isValidName("a:b"));
    CHECK(!isValidName("a|b"));
    CHECK(!isValidName("H\xce\xb1"));
    CHECK_EQ(sanitizeName("  Ha 7nm, narrow band filter "), std::string("Ha 7nm- narrow"));
    CHECK_EQ(sanitizeName("a:b|c#"), std::string("a-b-c-"));
    auto n = parseNames("L,R,,B", 5);
    CHECK(n.size() == 5 && n[0] == "L" && n[2].empty() && n[3] == "B" && n[4].empty());
    CHECK_EQ(parseNames("L,R,G", 2).size(), size_t(2));
    std::vector<int> o;
    CHECK(parseOffsets("0,-35,120", 4, o) && o.size() == 4 && o[1] == -35 && o[3] == 0);
    CHECK(!parseOffsets("0,x", 2, o));
    CHECK(!parseOffsets("40000", 1, o));
}

TEST(command_builders)
{
    CHECK_EQ(cmd::moveToSlot(3), std::string(":07 3#"));
    CHECK_EQ(cmd::setName(0, "Ha"), std::string(":37 0,Ha#"));
    CHECK_EQ(cmd::setFocusOffset(2, -40), std::string(":39 2,-40#"));
    CHECK(cmd::isValid(":33#"));
    CHECK(cmd::isValid(cmd::setName(8, "123456789012345")));
    CHECK(!cmd::isValid(":" + std::string(47, '1') + "#"));
    CHECK(!cmd::isValid("33#"));
    CHECK(!cmd::isValid(":37 0,a#b#"));
}

// ===================================================================== detection

TEST(detect_fake_sysfs)
{
    char tmpl[] = "/tmp/owdetXXXXXX";
    REQUIRE(mkdtemp(tmpl));
    std::string root = tmpl;
    std::string sys = root + "/sys/class/tty", dev = root + "/dev", devices = root + "/sys/devices";
    sh(("mkdir -p " + sys + " " + dev + "/serial/by-id " + devices));

    auto usbDevice = [&](const std::string &tty, const std::string &usb, const char *vid, const char *pid, const char *serial)
    {
        std::string u = devices + "/" + usb;
        sh(("mkdir -p " + u + "/" + usb + ":1.0/tty/" + tty));
        writeFile(u + "/idVendor", vid);
        writeFile(u + "/idProduct", pid);
        writeFile(u + "/serial", serial);
        sh(("mkdir -p " + sys + "/" + tty));
        std::string rc = "ln -s " + u + "/" + usb + ":1.0 " + sys + "/" + tty + "/device";
        sh(rc);
        writeFile(dev + "/" + tty, "");
    };
    usbDevice("ttyACM0", "1-1", "0483", "5740", "STM");          // generic ST VCP: ignored
    usbDevice("ttyACM1", "1-2", "1209", "0001", "TESTPID");       // test PID
    usbDevice("ttyACM2", "1-3", "1209", "2180", "REAL");          // registered PID
    usbDevice("ttyUSB0", "1-4", "1209", "2181", "UPBOX");         // other pid.codes product
    sh(("ln -s ../../ttyACM2 " + dev + "/serial/by-id/usb-MartinsOpenAstroTools_OpenWheel_REAL-if00"));

    auto c = findCandidates("", sys, dev);
    REQUIRE(c.size() == 2);
    CHECK_EQ(c[0].serial, std::string("REAL"));         // registered PID first
    CHECK_EQ(c[0].device, dev + "/ttyACM2");
    CHECK(c[0].byId.find("OpenWheel_REAL") != std::string::npos);
    CHECK_EQ(c[1].pid, UsbPidTest);
    CHECK(c[1].byId.empty());

    auto p = findCandidates("TESTPID", sys, dev);
    REQUIRE(p.size() == 2);
    CHECK_EQ(p[0].serial, std::string("TESTPID"));      // preferred serial wins

    CHECK(findCandidates("", root + "/nonexistent", dev).empty());
    sh(("rm -rf " + root));
}

// ===================================================================== session against the simulator

TEST(connect_reads_identity_names_housekeeping)
{
    Rig r;
    REQUIRE(r.ok);
    CHECK(r.wheel.connected());
    CHECK_EQ(r.wheel.identity().serial, std::string("3A00FF12ABCD"));
    CHECK_EQ(r.wheel.status().slots, 5);
    CHECK(r.wheel.status().state == WheelState::Idle);
    CHECK(r.wheel.configSupported() && r.wheel.configValid());
    CHECK_EQ(r.wheel.filterNames().size(), size_t(5));
    CHECK(r.wheel.housekeepingValid());
    CHECK(r.wheel.problem().empty());
}

TEST(move_reports_busy_then_arrives)
{
    Rig r;
    REQUIRE(r.ok);
    long long t0 = monotonicMs();
    Result m = r.wheel.moveTo(3);
    CHECK(m.ok);
    CHECK(r.wheel.status().busy());               // seen at once, before the next poll
    CHECK_EQ(r.wheel.status().position(), -1);
    CHECK_EQ(r.wheel.status().target, 3);
    CHECK(waitFor(r.wheel, [&] { return r.wheel.status().state == WheelState::Idle; }, 3000));
    CHECK_EQ(r.wheel.status().position(), 3);
    CHECK(monotonicMs() - t0 >= 250);              // 3 slots x 100 ms
    CHECK(r.wheel.moveTo(3).ok);                   // already there: nothing sent
    int moves = 0;
    r.sim.with([&](SimWheel &w) { moves = w.moves(); });
    CHECK_EQ(moves, 1);
}

TEST(move_rejections)
{
    Rig r;
    REQUIRE(r.ok);
    int before = r.sim.received;
    Result bad = r.wheel.moveTo(5);
    CHECK(!bad.ok && bad.code == ErrBadParam);
    CHECK_EQ(r.sim.received.load(), before);       // validated locally, nothing sent
    CHECK(r.wheel.moveTo(2).ok);
    Result busy = r.wheel.moveTo(4);
    CHECK(!busy.ok && busy.code == ErrBusy);       // the wheel's own ERR,8
    CHECK(r.wheel.connected());                    // a rejection is not a link failure
}

TEST(halt_and_home)
{
    Rig r;
    REQUIRE(r.ok);
    CHECK(r.wheel.moveTo(4).ok);
    CHECK(r.wheel.halt().ok);
    CHECK(r.wheel.status().state == WheelState::Idle);
    CHECK(!r.wheel.status().positionKnown());
    CHECK_EQ(r.wheel.status().position(), -1);
    CHECK(r.wheel.home().ok);
    CHECK(r.wheel.status().state == WheelState::Homing);
    CHECK(waitFor(r.wheel, [&] { return r.wheel.status().state == WheelState::Idle; }, 3000));
    CHECK_EQ(r.wheel.status().position(), 0);
}

TEST(names_offsets_roundtrip_and_only_changes_sent)
{
    Rig r;
    REQUIRE(r.ok);
    std::vector<std::string> n = { "L", "R", "G", "B", "Ha 7nm" };
    std::vector<int> o = { 0, 15, -20, 30, 120 };
    Result s = r.wheel.saveFilterConfig(n, o);
    CHECK(s.ok);
    CHECK(r.wheel.filterNames() == n);
    CHECK(r.wheel.focusOffsets() == o);
    r.sim.with([&](SimWheel &w) { CHECK_EQ(w.names()[4], std::string("Ha 7nm")); CHECK_EQ(w.offsets()[2], -20); });

    int before = r.sim.received;
    n[1] = "Red";
    CHECK(r.wheel.saveFilterConfig(n, o).ok);
    CHECK_EQ(r.sim.received.load() - before, 3);   // one :37, then :36 + :38 read back

    before = r.sim.received;
    n[0] = "bad,name";
    Result bad = r.wheel.saveFilterConfig(n, o);
    CHECK(!bad.ok);
    CHECK_EQ(r.sim.received.load(), before);       // rejected before anything is sent
    o[0] = 40000;
    n[0] = "L";
    CHECK(!r.wheel.saveFilterConfig(n, o).ok);
}

TEST(save_refused_while_moving)
{
    Rig r;
    REQUIRE(r.ok);
    CHECK(r.wheel.moveTo(4).ok);
    Result s = r.wheel.saveFilterConfig({ "a", "b", "c", "d", "e" }, { 0, 0, 0, 0, 0 });
    CHECK(!s.ok && s.code == ErrBusy);
}

TEST(device_fault_and_clear)
{
    Rig r;
    REQUIRE(r.ok);
    r.sim.with([](SimWheel &w) { w.failNextMove(ErrNoHall); });
    CHECK(r.wheel.moveTo(2).ok);
    CHECK(waitFor(r.wheel, [&] { return r.wheel.status().state == WheelState::Error; }, 3000));
    CHECK_EQ(r.wheel.status().error, int(ErrNoHall));
    CHECK(r.wheel.problem().find("hall") != std::string::npos);
    // the firmware leaves the error state after its hold time, the code stays sticky
    CHECK(waitFor(r.wheel, [&] { return r.wheel.status().state == WheelState::Idle; }, 2000));
    CHECK_EQ(r.wheel.status().error, int(ErrNoHall));
    CHECK(r.wheel.clearError().ok);
    CHECK_EQ(r.wheel.status().error, 0);
    CHECK(r.wheel.problem().empty());
}

TEST(watchdog_halts_a_stuck_move)
{
    WheelOptions o = fastOptions();
    o.moveTimeoutMs = 500;
    Rig r(fastSim(), o);
    REQUIRE(r.ok);
    r.sim.with([](SimWheel &w) { w.setStuck(true); });
    CHECK(r.wheel.moveTo(1).ok);
    CHECK(waitFor(r.wheel, [&] { return r.wheel.watchdogTripped(); }, 3000));
    CHECK(waitFor(r.wheel, [&] { return r.wheel.status().state == WheelState::Idle; }, 1000));
    CHECK(r.wheel.problem().find("halted") != std::string::npos);
    auto h = r.sim.history();
    bool halted = false;
    for (auto &c : h) halted |= c == "30";
    CHECK(halted);
    r.sim.with([](SimWheel &w) { w.setStuck(false); });
    CHECK(r.wheel.moveTo(2).ok);                   // an accepted command clears the note
    CHECK(r.wheel.problem().empty());
}

TEST(calibration_from_uncalibrated)
{
    SimConfig c = fastSim();
    Rig r(c);
    REQUIRE(r.ok);
    r.sim.with([](SimWheel &w) { w.setSlotCount(0); });
    CHECK(waitFor(r.wheel, [&] { return r.wheel.status().slots == 0; }, 1000));
    Result m = r.wheel.moveTo(0);
    CHECK(!m.ok && m.code == ErrNotCalibrated);
    CHECK(r.wheel.problem().find("not calibrated") != std::string::npos);
    CHECK(r.wheel.calibrate().ok);
    CHECK(r.wheel.status().state == WheelState::Calibrating);
    CHECK(waitFor(r.wheel, [&] { return r.wheel.status().slots == 5 && r.wheel.configValid(); }, 3000));
    CHECK(r.wheel.problem().empty());
}

TEST(housekeeping_warning_follows_status_flag)
{
    Rig r;
    REQUIRE(r.ok);
    CHECK_EQ(r.wheel.housekeeping().warnings, 0);
    r.sim.with([](SimWheel &w) { w.setWarnings(WarnSupplyLow); });
    CHECK(waitFor(r.wheel, [&] { return r.wheel.housekeeping().warnings == WarnSupplyLow; }, 1500));
}

TEST(old_firmware_without_names_or_housekeeping)
{
    SimConfig c = fastSim();
    c.filterConfig = false;
    c.housekeeping = false;
    Rig r(c);
    REQUIRE(r.ok);
    CHECK(!r.wheel.configSupported());
    CHECK(!r.wheel.housekeepingSupported());
    CHECK(!r.wheel.saveFilterConfig({ "a", "b", "c", "d", "e" }, { 0, 0, 0, 0, 0 }).ok);
    CHECK(r.wheel.moveTo(1).ok);
}

// ===================================================================== identify first (#16)

TEST(nothing_but_ping_and_identify_before_identify)
{
    Rig r;
    REQUIRE(r.ok);
    int v = -1;
    r.sim.with([&](SimWheel &s) { v = s.violations(); });
    CHECK_EQ(v, 0);
    auto h = r.sim.history();
    size_t idPos = 0;
    while (idPos < h.size() && h[idPos] != "32") idPos++;
    REQUIRE(idPos < h.size());
    for (size_t i = 0; i < idPos; i++) CHECK(h[i] == "25");
    CHECK_EQ(r.wheel.identity().protocol, 3);
}

TEST(locked_wheel_answers_err16)
{
    SimServer sim(fastSim());
    std::string why;
    REQUIRE(sim.start(why));
    SerialLink l;
    REQUIRE(l.open(sim.path(), why));
    Reply s = l.execute(cmd::Status, 300);
    CHECK(s.kind == Reply::Error && s.code == ErrNotIdentified);
    CHECK(l.execute(cmd::Ping, 300).isOk());
    CHECK(l.execute(cmd::Identify, 300).kind == Reply::Value);
    CHECK(l.execute(cmd::Status, 300).kind == Reply::Value);
    CHECK(describeError(16).find("identif") != std::string::npos);
    CHECK(!isFault(16));
}

TEST(relock_mid_session_is_identified_again)
{
    Rig r;
    REQUIRE(r.ok);
    r.sim.with([](SimWheel &s) { s.relock(); });
    Result m = r.wheel.moveTo(2);
    CHECK(m.ok);
    CHECK(waitFor(r.wheel, [&] { return r.wheel.status().state == WheelState::Idle && r.wheel.status().position() == 2; }, 3000));
    CHECK(r.wheel.connected());
}

TEST(different_device_after_relock_drops_the_connection)
{
    Rig r;
    REQUIRE(r.ok);
    r.sim.with([](SimWheel &s) { s.relock(); s.cfg.serial = "OTHER0002"; });
    CHECK(waitFor(r.wheel, [&] { return !r.wheel.connected(); }, 2000));
    CHECK(r.wheel.problem().find("different device") != std::string::npos);
}

TEST(protocol2_without_lock_still_connects)
{
    SimConfig c = fastSim();
    c.protocol = 2;
    c.identifyFirst = false;
    c.firmware = "FW1.3.0";
    Rig r(c);
    REQUIRE(r.ok);
    CHECK_EQ(r.wheel.identity().protocol, 2);
}

TEST(unsupported_protocol_is_refused)
{
    SimConfig c = fastSim();
    c.protocol = 1;
    Rig r(c);
    CHECK(!r.ok);
    CHECK(!r.wheel.connected());
}

TEST(silent_device_is_refused_in_bounded_time)
{
    SimServer sim(fastSim());
    std::string why;
    REQUIRE(sim.start(why));
    sim.silent = true;
    Wheel w(fastOptions());
    long long t0 = monotonicMs();
    CHECK(!w.open(sim.path(), why));
    long long took = monotonicMs() - t0;
    CHECK(took < 3 * 300 + 300);                   // three pings, nothing more
    CHECK(why.find("no answer") != std::string::npos);
}

TEST(not_an_openwheel_is_refused)
{
    // a device that answers everything with garbage (another product on the same port)
    SimServer sim(fastSim());
    std::string why;
    REQUIRE(sim.start(why));
    sim.garbage = true;
    Wheel w(fastOptions());
    CHECK(!w.open(sim.path(), why));
}

TEST(silence_while_connected_loses_connection)
{
    Rig r;
    REQUIRE(r.ok);
    r.sim.silent = true;
    long long t0 = monotonicMs();
    CHECK(waitFor(r.wheel, [&] { return !r.wheel.connected(); }, 5000));
    CHECK(monotonicMs() - t0 < 3000);
    CHECK(r.wheel.problem().find("connection lost") != std::string::npos);
    Result m = r.wheel.moveTo(1);
    CHECK(!m.ok);
}

TEST(garbage_and_lost_hash_count_as_failures_then_recover)
{
    Rig r;
    REQUIRE(r.ok);
    r.sim.garbage = true;
    CHECK(!r.wheel.moveTo(1).ok);                  // :07 garbled, :33 garbled
    CHECK(r.wheel.connected());                    // two failures, not three
    r.sim.garbage = false;
    // the wheel did start the move (only the answers were garbled); a good poll resets the counter
    CHECK(waitFor(r.wheel, [&] { return r.wheel.status().state == WheelState::Idle && r.wheel.status().position() == 1; }, 3000));
    r.sim.noHash = true;
    long long t0 = monotonicMs();
    Result h = r.wheel.home();
    CHECK(!h.ok);
    CHECK(monotonicMs() - t0 < 400);               // bounded by the command timeout
    r.sim.noHash = false;
    CHECK(waitFor(r.wheel, [&] { return r.wheel.status().state == WheelState::Idle; }, 3000));
    CHECK(r.wheel.connected());
}

TEST(late_reply_is_not_taken_for_the_next_one)
{
    Rig r;
    REQUIRE(r.ok);
    r.sim.delayMs = 450;                           // longer than the 300 ms timeout
    long long t0 = monotonicMs();
    Result m = r.wheel.moveTo(2);                  // the move starts, its OK comes too late
    CHECK(monotonicMs() - t0 < 1000);
    r.sim.delayMs = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    // the next status must be a real status line, not the stale OK
    CHECK(waitFor(r.wheel, [&] { return r.wheel.status().state == WheelState::Idle && r.wheel.status().position() == 2; }, 3000));
    CHECK(r.wheel.connected());
    (void)m;
}

TEST(unplug_ends_the_connection)
{
    Rig r;
    REQUIRE(r.ok);
    r.sim.unplug();
    long long t0 = monotonicMs();
    Result m = r.wheel.moveTo(1);
    CHECK(!m.ok);
    CHECK(monotonicMs() - t0 < 1000);
    CHECK(!r.wheel.connected());
    CHECK(r.wheel.problem().find("disconnected") != std::string::npos || r.wheel.problem().find("lost") != std::string::npos);
}

TEST(framing_noise_is_ignored_by_the_simulator)
{
    // the firmware ignores bytes outside :...# and restarts at ':'; the simulator does too
    SimServer sim(fastSim());
    std::string why;
    REQUIRE(sim.start(why));
    SerialLink l;
    REQUIRE(l.open(sim.path(), why));
    Reply rr = l.execute(":25#", 300);
    CHECK(rr.isOk());
    CHECK(l.execute(":32#", 300).kind == Reply::Value);      // identify first (protocol 3)
    Reply s = l.execute(":33#", 300);
    Status st;
    CHECK(s.kind == Reply::Value && Status::parse(s.value, st));
    CHECK(l.execute(":99#", 300).code == ErrUnknownCommand);
}

int main(int argc, char **argv) { return check::runAll(argc, argv); }
