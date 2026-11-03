#include "drivers/openrgb/domain/settings.hpp"

namespace wpc::openrgb {

ClientSettings read_settings(const DriverSettings& table) {
    ClientSettings s;
    if (std::optional<std::string> address = table.string("address")) s.address = *address;
    if (std::optional<std::string> name = table.string("client_name")) s.client_name = *name;
    return s;
}

}  // namespace wpc::openrgb
