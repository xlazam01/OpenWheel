#include "detect.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <unistd.h>

namespace openwheel
{

namespace
{
std::string readLine(const std::string &path)
{
    std::ifstream f(path);
    std::string s;
    if (f) std::getline(f, s);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

bool fileExists(const std::string &p) { return access(p.c_str(), F_OK) == 0; }

std::string realPath(const std::string &p)
{
    char buf[PATH_MAX];
    if (!realpath(p.c_str(), buf)) return std::string();
    return buf;
}

std::string parentDir(const std::string &p)
{
    size_t s = p.find_last_of('/');
    return (s == std::string::npos || s == 0) ? std::string("/") : p.substr(0, s);
}

/// Walks up from the tty's device directory to the USB device that carries idVendor/idProduct.
std::string usbDeviceDir(const std::string &ttyDir)
{
    std::string d = realPath(ttyDir + "/device");
    for (int i = 0; i < 4 && !d.empty() && d != "/"; i++)
    {
        if (fileExists(d + "/idVendor") && fileExists(d + "/idProduct")) return d;
        d = parentDir(d);
    }
    return std::string();
}

std::string findById(const std::string &devRoot, const std::string &device)
{
    std::string dir = devRoot + "/serial/by-id";
    DIR *dp = opendir(dir.c_str());
    if (!dp) return std::string();
    std::string target = realPath(device), found;
    while (struct dirent *e = readdir(dp))
    {
        if (e->d_name[0] == '.') continue;
        std::string link = dir + "/" + e->d_name;
        if (!target.empty() && realPath(link) == target) { found = link; break; }
    }
    closedir(dp);
    return found;
}
}

bool isCandidate(const UsbPort &p)
{
    return p.vid == UsbVid && (p.pid == UsbPidOpenWheel || p.pid == UsbPidTest);
}

std::vector<UsbPort> findCandidates(const std::string &preferredSerial, const std::string &sysClassTty,
                                    const std::string &devRoot)
{
    std::vector<UsbPort> out;
    DIR *dp = opendir(sysClassTty.c_str());
    if (!dp) return out;
    while (struct dirent *e = readdir(dp))
    {
        std::string name = e->d_name;
        if (name.compare(0, 6, "ttyACM") != 0 && name.compare(0, 6, "ttyUSB") != 0) continue;
        std::string usb = usbDeviceDir(sysClassTty + "/" + name);
        if (usb.empty()) continue;
        UsbPort p;
        p.device = devRoot + "/" + name;
        p.vid = static_cast<int>(std::strtol(readLine(usb + "/idVendor").c_str(), nullptr, 16));
        p.pid = static_cast<int>(std::strtol(readLine(usb + "/idProduct").c_str(), nullptr, 16));
        p.serial = readLine(usb + "/serial");
        if (!isCandidate(p)) continue;
        p.byId = findById(devRoot, p.device);
        out.push_back(p);
    }
    closedir(dp);

    std::sort(out.begin(), out.end(), [&](const UsbPort &a, const UsbPort &b)
    {
        bool pa = !preferredSerial.empty() && a.serial == preferredSerial;
        bool pb = !preferredSerial.empty() && b.serial == preferredSerial;
        if (pa != pb) return pa;
        if (a.pid != b.pid) return a.pid == UsbPidOpenWheel;
        return a.device < b.device;
    });
    return out;
}

}
