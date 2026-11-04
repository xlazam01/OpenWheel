// openwheel_sim: a simulated OpenWheel on a pseudo terminal, for testing the INDI driver (and any
// other host software) without hardware.
//
//   openwheel_sim [--slots N] [--ms-per-slot N] [--link PATH] [--control FIFO] [--old-firmware]
//
// Prints the pty path (e.g. /dev/pts/3) on the first line; with --link a symlink to it is created
// (e.g. /tmp/openwheel). Control lines are read from stdin and from the --control FIFO:
//   silent 0|1, garbage 0|1, nohash 0|1, delay <ms>   link faults
//   fault <code>      sticky fault now (error state for 10 s)
//   failnext <code>   the next move/home/calibration ends with this fault (6 no hall, 13 stalled)
//   stuck 0|1         moves never finish (driver watchdog test)
//   warn <bits>       housekeeping warnings (status flag bit 4)
//   slots <n>         change the calibrated slot count (0 = not calibrated)
//   show              print the simulated state
//   unplug | quit     close the pty (the driver sees a USB unplug) and exit
#include "sim_wheel.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace openwheel;

static volatile sig_atomic_t quit = 0;
static void onSignal(int) { quit = 1; }

static bool control(SimServer &s, const std::string &line)
{
    std::istringstream in(line);
    std::string c;
    long v = 0;
    in >> c;
    in >> v;
    if (c.empty()) return true;
    if (c == "silent") s.silent = v != 0;
    else if (c == "garbage") s.garbage = v != 0;
    else if (c == "nohash") s.noHash = v != 0;
    else if (c == "delay") s.delayMs = static_cast<int>(v);
    else if (c == "fault") s.with([&](SimWheel &w) { w.injectFault(static_cast<int>(v), simNowMs()); });
    else if (c == "failnext") s.with([&](SimWheel &w) { w.failNextMove(static_cast<int>(v)); });
    else if (c == "stuck") s.with([&](SimWheel &w) { w.setStuck(v != 0); });
    else if (c == "warn") s.with([&](SimWheel &w) { w.setWarnings(static_cast<int>(v)); });
    else if (c == "slots") s.with([&](SimWheel &w) { w.setSlotCount(static_cast<int>(v)); });
    else if (c == "show")
        s.with([&](SimWheel &w)
        {
            std::printf("state %d slot %d known %d error %d moves %d names", w.state(), w.slot(), w.positionKnown(), w.error(), w.moves());
            for (const auto &n : w.names()) std::printf(" [%s]", n.c_str());
            std::printf(" offsets");
            for (int o : w.offsets()) std::printf(" %d", o);
            std::printf("\n");
            std::fflush(stdout);
        });
    else if (c == "unplug" || c == "quit") return false;
    else { std::fprintf(stderr, "unknown control command: %s\n", c.c_str()); }
    return true;
}

int main(int argc, char **argv)
{
    SimConfig cfg;
    std::string link, fifo;
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--slots") cfg.slots = std::atoi(next().c_str());
        else if (a == "--ms-per-slot") cfg.msPerSlot = std::atoi(next().c_str());
        else if (a == "--link") link = next();
        else if (a == "--control") fifo = next();
        else if (a == "--old-firmware") { cfg.filterConfig = false; cfg.housekeeping = false; cfg.firmware = "FW1.0.1"; }
        else if (a == "--serial") cfg.serial = next();
        else { std::fprintf(stderr, "usage: %s [--slots N] [--ms-per-slot N] [--link PATH] [--control FIFO] [--old-firmware] [--serial S]\n", argv[0]); return 2; }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGPIPE, SIG_IGN);

    SimServer server(cfg);
    std::string why;
    if (!server.start(why)) { std::fprintf(stderr, "%s\n", why.c_str()); return 1; }
    if (!link.empty())
    {
        unlink(link.c_str());
        if (symlink(server.path().c_str(), link.c_str()) != 0) { std::perror("symlink"); return 1; }
    }
    int cfd = -1;
    if (!fifo.empty())
    {
        if (access(fifo.c_str(), F_OK) != 0 && mkfifo(fifo.c_str(), 0600) != 0) { std::perror("mkfifo"); return 1; }
        cfd = open(fifo.c_str(), O_RDWR | O_NONBLOCK);   // RDWR: never sees EOF when writers come and go
    }
    std::printf("%s\n", server.path().c_str());
    std::fflush(stdout);

    std::string bufIn, bufCtl;
    bool stdinOpen = true;
    while (!quit)
    {
        struct pollfd p[2] = { { stdinOpen ? 0 : -1, POLLIN, 0 }, { cfd, POLLIN, 0 } };
        if (poll(p, 2, 200) <= 0) continue;
        for (int k = 0; k < 2; k++)
        {
            if (!(p[k].revents & (POLLIN | POLLHUP))) continue;
            char b[256];
            ssize_t n = read(p[k].fd, b, sizeof(b));
            if (n <= 0) { if (k == 0) stdinOpen = false; continue; }
            std::string &acc = k == 0 ? bufIn : bufCtl;
            acc.append(b, static_cast<size_t>(n));
            size_t nl;
            while ((nl = acc.find('\n')) != std::string::npos)
            {
                std::string line = acc.substr(0, nl);
                acc.erase(0, nl + 1);
                if (!control(server, line)) quit = 1;
            }
        }
    }
    server.unplug();
    if (!link.empty()) unlink(link.c_str());
    if (cfd >= 0) close(cfd);
    return 0;
}
