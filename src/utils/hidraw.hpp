// USB HID devices through /dev/hidraw*: finding one by its USB ids, and HID feature reports.
// Used by drivers that talk to hardware directly (fusion2, razer).

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

#include "utils/socket.hpp"

namespace wpc::hidraw {

struct UsbId {
    uint16_t vendor;
    uint16_t product;
    /// The USB interface to use, for devices with several (a keyboard has one for its keys and
    /// another for its lights); -1: any.
    int interface = -1;
};

/// The /dev/hidraw* of the first device with one of `ids` (and its interface, if given), found
/// through /sys; nullopt if none is plugged in.
std::optional<std::filesystem::path> find(std::span<const UsbId> ids);

/// Opens it for reading and writing. Throws wpc::Error; on EACCES the message says which
/// udev rule is missing (`udev_hint`).
Fd open(const std::filesystem::path& path, std::string_view udev_hint);

/// Sends a feature report (its first byte is the report id). Throws wpc::Error.
void set_feature(const Fd& device, std::span<const uint8_t> report);

/// False when the device `device` was opened on is gone (unplugged; plugged in again it is a
/// new device, with a new file).
bool connected(const Fd& device);

/// Reads a feature report into `report`, whose first byte says which. Throws wpc::Error.
void get_feature(const Fd& device, std::span<uint8_t> report);

}  // namespace wpc::hidraw
