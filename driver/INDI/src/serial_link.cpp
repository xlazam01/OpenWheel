#include "serial_link.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace openwheel
{

namespace
{
long long nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
}

SerialLink::~SerialLink() { close(); }

bool SerialLink::open(const std::string &path, std::string &why)
{
    close();
    int f = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (f < 0)
    {
        why = path + ": " + std::strerror(errno);
        return false;
    }
    struct termios tio;
    if (tcgetattr(f, &tio) == 0)
    {
        cfmakeraw(&tio);
        cfsetispeed(&tio, B115200);
        cfsetospeed(&tio, B115200);
        tio.c_cflag |= CLOCAL | CREAD;
        tio.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
        tio.c_cc[VMIN] = 0;
        tio.c_cc[VTIME] = 0;
        tcsetattr(f, TCSANOW, &tio);
    }
    fd = f;
    owned = true;
    isBroken = false;
    portName = path;
    return true;
}

void SerialLink::attach(int f, const std::string &name)
{
    close();
    fd = f;
    owned = false;
    isBroken = false;
    portName = name;
    // INDI opens the port blocking; the link only ever waits through poll().
    int fl = fcntl(fd, F_GETFL);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

void SerialLink::close()
{
    if (fd >= 0 && owned) ::close(fd);
    fd = -1;
    owned = false;
}

void SerialLink::discardInput()
{
    tcflush(fd, TCIFLUSH);
    char buf[256];
    // tcflush does not reach bytes still in flight on some USB CDC drivers and ptys: drain them too.
    for (int i = 0; i < 16; i++)
    {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) break;
    }
}

bool SerialLink::writeAll(const std::string &data, int timeoutMs)
{
    long long deadline = nowMs() + timeoutMs;
    size_t done = 0;
    while (done < data.size())
    {
        ssize_t n = ::write(fd, data.data() + done, data.size() - done);
        if (n > 0) { done += static_cast<size_t>(n); continue; }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { isBroken = true; return false; }
        long long left = deadline - nowMs();
        if (left <= 0) return false;
        struct pollfd p = { fd, POLLOUT, 0 };
        poll(&p, 1, static_cast<int>(left));
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) { isBroken = true; return false; }
    }
    return true;
}

Reply SerialLink::execute(const std::string &command, int timeoutMs)
{
    if (fd < 0 || isBroken) return Reply::timeout();
    if (!cmd::isValid(command)) return Reply::garbage();

    long long deadline = nowMs() + timeoutMs;
    discardInput();
    if (!writeAll(command, timeoutMs)) return Reply::timeout();

    std::string line;
    bool overflowed = false;
    char buf[64];
    while (true)
    {
        long long left = deadline - nowMs();
        if (left <= 0) return overflowed ? Reply::garbage() : Reply::timeout();

        struct pollfd p = { fd, POLLIN, 0 };
        int pr = poll(&p, 1, static_cast<int>(left));
        if (pr < 0)
        {
            if (errno == EINTR) continue;
            isBroken = true;
            return Reply::timeout();
        }
        if (pr == 0) continue;
        if (p.revents & POLLNVAL) { isBroken = true; return Reply::timeout(); }

        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            isBroken = true;   // EIO: device gone
            return Reply::timeout();
        }
        if (n == 0)
        {
            // End of file / hang-up without data: the other side is gone.
            if (p.revents & (POLLHUP | POLLERR)) { isBroken = true; return Reply::timeout(); }
            continue;
        }
        for (ssize_t i = 0; i < n; i++)
        {
            char c = buf[i];
            if (c == '#') return overflowed ? Reply::garbage() : Reply::parse(line);
            if (line.size() < static_cast<size_t>(MaxLineLength)) line += c;
            else overflowed = true;
        }
    }
}

}
