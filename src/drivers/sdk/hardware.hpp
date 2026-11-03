// What WallpaperColor paints, in its own words: components (fans, pump, RAM…), and the
// devices and zones a driver finds. Plain values, no I/O.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "shared/domain/colours/color.hpp"

namespace wpc {

/// What the user turns on or off. New values go at the end.
enum class Component : uint8_t { Fans, Pump, Ram, Motherboard, Argb, Keyboard, Gpu, Strip, Other };

inline constexpr std::array ALL_COMPONENTS = {
    Component::Fans,     Component::Pump, Component::Ram,   Component::Motherboard, Component::Argb,
    Component::Keyboard, Component::Gpu,  Component::Strip, Component::Other,
};

/// Its name in the config file and on the socket: "fans", "pump"…
inline std::string_view component_name(Component c) {
    constexpr std::string_view names[] = {"fans", "pump", "ram",   "motherboard", "argb",
                                          "keyboard", "gpu", "strip", "other"};
    return names[static_cast<size_t>(c)];
}

/// Its name for people: "Fans", "ARGB headers"…
inline std::string_view component_label(Component c) {
    constexpr std::string_view labels[] = {"Fans",     "Pump", "RAM",        "Motherboard", "ARGB headers",
                                           "Keyboard", "GPU",  "LED strips", "Other"};
    return labels[static_cast<size_t>(c)];
}

inline std::optional<Component> component_from_name(std::string_view name) {
    for (Component c : ALL_COMPONENTS) {
        if (component_name(c) == name) return c;
    }
    return std::nullopt;
}

/// How colours spread over a zone's LEDs.
enum class Shape : uint8_t {
    Ring,  // a loop (fans, pumps): a gradient goes all the way round
    Line,  // one end to the other (RAM top to bottom, strips, keyboards)
    Spot,  // one colour (single-colour lights)
};

inline std::string_view shape_name(Shape s) {
    constexpr std::string_view names[] = {"ring", "line", "spot"};
    return names[static_cast<size_t>(s)];
}

inline std::optional<Shape> shape_from_name(std::string_view name) {
    for (Shape s : {Shape::Ring, Shape::Line, Shape::Spot}) {
        if (shape_name(s) == name) return s;
    }
    return std::nullopt;
}

struct Zone {
    std::string name;  // "8-LED Series Fan"
    uint32_t leds = 0;
    Component component = Component::Other;  // the driver's guess; [[component]] can change it
    Shape shape = Shape::Ring;
};

struct Device {
    /// Stable within the driver while the hardware doesn't change: fades remember LEDs by it.
    std::string key;
    std::string name;  // "iCUE COMMANDER Core"
    std::vector<Zone> zones;
    /// What every LED shows now, zone by zone; empty if the device cannot tell.
    std::vector<Rgb> reported;
};

}  // namespace wpc
