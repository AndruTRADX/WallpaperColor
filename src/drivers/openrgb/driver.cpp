#include "drivers/openrgb/driver.hpp"

#include "utils/error.hpp"
#include "utils/text.hpp"

namespace wpc::openrgb {

namespace {

/// The zones' colours as one span when each zone's follow the previous one's in memory (the
/// service sends every frame as one buffer); empty otherwise.
std::span<const Rgb> one_span(std::span<const ZoneFrame> zones) {
    if (zones.empty()) return {};
    const Rgb* end = zones.front().colours.data();
    for (const ZoneFrame& z : zones) {
        if (z.colours.data() != end) return {};
        end += z.colours.size();
    }
    return std::span(zones.front().colours.data(), end);
}

}  // namespace

std::vector<Device> ServerDriver::devices() {
    try {
        if (!server_->is_open()) server_->open(settings_.address, settings_.client_name);
        controllers_ = server_->controllers();
    } catch (const Error& e) {
        server_->close();
        throw Error(cat("cannot reach the OpenRGB server at ", settings_.address, ": ", e.what()));
    }
    std::vector<Device> out;
    zone_counts_.clear();
    for (const Controller& c : controllers_) {
        out.push_back(Device{cat(c.index, " ", c.name), c.name, classify(c), c.colors});
        zone_counts_.push_back(out.back().zones.size());
    }
    return out;
}

void ServerDriver::show(size_t device, std::span<const ZoneFrame> zones) {
    if (!server_->is_open()) throw Error("OpenRGB: not connected");
    if (device >= controllers_.size()) throw Error(cat("OpenRGB: no device ", device));
    const Controller& controller = controllers_[device];
    bool whole = zones.size() == zone_counts_[device];
    for (size_t i = 0; whole && i < zones.size(); ++i) whole = zones[i].zone == i;
    try {
        if (whole) {
            std::span<const Rgb> all = one_span(zones);
            if (all.empty()) {
                joined_.clear();
                for (const ZoneFrame& z : zones) joined_.insert(joined_.end(), z.colours.begin(), z.colours.end());
                all = joined_;
            }
            server_->set_leds(controller.index, all);
            return;
        }
        for (const ZoneFrame& z : zones) server_->set_zone_leds(controller.index, static_cast<uint32_t>(z.zone), z.colours);
    } catch (const Error& e) {
        server_->close();  // the next devices() connects again
        throw Error(cat("cannot reach the OpenRGB server at ", settings_.address, ": ", e.what()));
    }
}

}  // namespace wpc::openrgb
