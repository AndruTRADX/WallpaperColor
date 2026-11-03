// `set`: paint every chosen component once.

#include <cstdlib>
#include <format>

#include "terminal/presentation/cli/commands.hpp"
#include "terminal/presentation/cli/output.hpp"

namespace wpc {

int run_set(TerminalParts& parts, const Config& config, const ColoursToSet& what) {
    SetResult result = set_colours(parts.drivers, parts.pictures, config, what);
    std::vector<std::string> label;
    for (Rgb c : result.stops) label.push_back(swatch(c, 2) + c.hex());
    for (const std::string& device : result.painted) out(std::format("{} → {}", join(label, " "), device));
    for (const std::string& problem : result.not_painted) out("not painted: " + problem);
    return EXIT_SUCCESS;
}

}  // namespace wpc
