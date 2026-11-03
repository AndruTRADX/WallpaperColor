// A driver's table of the config file ([openrgb], [motherboard]…) as plain values. The config
// reader fills it; the driver reads what it understands. Header-only, so driver modules can use
// it without linking anything.

#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "utils/error.hpp"

namespace wpc {

/// A driver's table in the config file ([openrgb], [motherboard]…), read by the core and
/// handed to the driver. Reading a key with the wrong type throws wpc::Error. Keys the driver
/// never reads are reported by the programs, so a typo doesn't go unnoticed.
class DriverSettings {
public:
    using Value = std::variant<bool, double, std::string, std::vector<double>>;

    DriverSettings() = default;
    DriverSettings(std::string table, std::map<std::string, Value> values)
        : table_(std::move(table)), values_(std::move(values)) {}

    std::optional<bool> boolean(const std::string& key) const { return get<bool>(key, "true or false"); }
    std::optional<double> number(const std::string& key) const { return get<double>(key, "a number"); }
    std::optional<std::string> string(const std::string& key) const { return get<std::string>(key, "a string"); }
    std::optional<std::vector<double>> numbers(const std::string& key) const {
        return get<std::vector<double>>(key, "a list of numbers, like [8, 8]");
    }

    /// Keys that are in the table but were never read.
    std::vector<std::string> unread() const {
        std::vector<std::string> out;
        for (const auto& [key, value] : values_) {
            if (!read_.contains(key)) out.push_back(key);
        }
        return out;
    }

    const std::string& table() const { return table_; }
    const std::map<std::string, Value>& values() const { return values_; }
    bool operator==(const DriverSettings& o) const { return table_ == o.table_ && values_ == o.values_; }

private:
    template <typename T>
    std::optional<T> get(const std::string& key, std::string_view expected) const {
        auto found = values_.find(key);
        if (found == values_.end()) return std::nullopt;
        read_.insert(key);
        if (const T* value = std::get_if<T>(&found->second)) return *value;
        throw Error(cat("[", table_, "] ", key, ": expected ", expected));
    }

    std::string table_;
    std::map<std::string, Value> values_;
    mutable std::set<std::string> read_;
};

}  // namespace wpc
