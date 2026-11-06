#include "utils/hidraw.hpp"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <string>

#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

#include "utils/error.hpp"

namespace wpc::hidraw {

namespace fs = std::filesystem;

namespace {

/// The USB interface number of a /sys/class/hidraw entry, or -1 if it isn't a USB device.
int interface_number(const fs::path& entry) {
    // device/ is the HID device; its parent is the USB interface, whose number is in hex.
    std::ifstream file(entry / "device" / ".." / "bInterfaceNumber");
    unsigned number = 0;
    if (!(file >> std::hex >> number)) return -1;
    return static_cast<int>(number);
}

}  // namespace

std::optional<fs::path> find(std::span<const UsbId> ids) {
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator("/sys/class/hidraw", ec)) {
        // The uevent file has a line like HID_ID=0003:0000048D:00005702 (bus:vendor:product).
        std::ifstream uevent(entry.path() / "device" / "uevent");
        std::string line;
        while (std::getline(uevent, line)) {
            if (!line.starts_with("HID_ID=")) continue;
            for (UsbId id : ids) {
                char wanted[18];  // "0000048D:00005702"
                std::snprintf(wanted, sizeof wanted, "%08X:%08X", unsigned(id.vendor), unsigned(id.product));
                if (line.ends_with(wanted) &&
                    (id.interface < 0 || interface_number(entry.path()) == id.interface)) {
                    return fs::path("/dev") / entry.path().filename();
                }
            }
        }
    }
    return std::nullopt;
}

Fd open(const fs::path& path, std::string_view udev_hint) {
    Fd fd(::open(path.c_str(), O_RDWR | O_CLOEXEC));
    if (!fd.valid()) {
        int error = errno;
        throw Error(cat("cannot open ", path.string(), ": ", os_error(error),
                        error == EACCES ? cat(" (", udev_hint, ")") : ""));
    }
    return fd;
}

void set_feature(const Fd& device, std::span<const uint8_t> report) {
    if (::ioctl(device.get(), HIDIOCSFEATURE(report.size()), report.data()) < 0) throw Error(os_error(errno));
}

bool connected(const Fd& device) {
    hidraw_devinfo info{};
    return ::ioctl(device.get(), HIDIOCGRAWINFO, &info) == 0;
}

void get_feature(const Fd& device, std::span<uint8_t> report) {
    if (::ioctl(device.get(), HIDIOCGFEATURE(report.size()), report.data()) < 0) throw Error(os_error(errno));
}

}  // namespace wpc::hidraw
