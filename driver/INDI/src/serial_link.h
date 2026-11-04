// POSIX serial link to the wheel: one command, one reply, always within the given time.
#pragma once

#include "protocol.h"

#include <string>

namespace openwheel
{

class SerialLink
{
    public:
        SerialLink() = default;
        ~SerialLink();
        SerialLink(const SerialLink &) = delete;
        SerialLink &operator=(const SerialLink &) = delete;

        /// Opens `path` raw at 115200 8N1. False (with `why`) when it cannot be opened.
        bool open(const std::string &path, std::string &why);
        /// Uses a descriptor opened by someone else (INDI's serial connection); not closed by close().
        void attach(int fd, const std::string &name);
        void close();

        bool isOpen() const { return fd >= 0; }
        /// True after a read/write error or hang-up (device unplugged). The link must be reopened.
        bool broken() const { return isBroken; }
        const std::string &name() const { return portName; }
        int descriptor() const { return fd; }

        /// Discards stale input, sends `command` (":NN...#") and waits up to `timeoutMs` for one
        /// reply line. Returns Timeout/Garbage instead of blocking; never waits longer than the timeout.
        Reply execute(const std::string &command, int timeoutMs);

    private:
        void discardInput();
        bool writeAll(const std::string &data, int timeoutMs);

        int fd = -1;
        bool owned = false;
        bool isBroken = false;
        std::string portName;
};

}
