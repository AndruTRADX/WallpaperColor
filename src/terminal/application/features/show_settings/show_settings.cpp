#include "terminal/application/features/show_settings/show_settings.hpp"

#include <cctype>

namespace wpc {

std::string show_settings(SettingsFormat& format, const Config& config) {
    std::string text = format.write(config);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    return text;
}

}  // namespace wpc
