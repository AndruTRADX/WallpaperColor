// `extract`: the colours that would be taken from pictures.

#include <cstdlib>
#include <format>
#include <iostream>

#include "colours/colour.hpp"
#include "terminal/app/hardware.hpp"
#include "terminal/cli/commands.hpp"
#include "terminal/cli/output.hpp"

namespace wpc {

int run_extract(TerminalParts& parts, const Config& config, const std::vector<std::filesystem::path>& pictures,
                bool palette) {
    bool failed = false;
    for (const PictureColours& p : extract_colours(parts.pictures, pictures, config.extract)) {
        if (!p.colours) {
            std::cerr << p.picture.string() << ": " << p.error << "\n";
            failed = true;
            continue;
        }
        const Extraction& e = *p.colours;
        std::string arc;
        for (Rgb c : e.arc) arc += swatch(c, 2);
        out(std::format("{}{}  arc {}{} {} {}  {}", swatch(e.led, 6), to_hex(e.led), arc, to_hex(e.arc[0]), to_hex(e.arc[1]),
                        to_hex(e.arc[2]), p.picture.string()));
        if (!palette) continue;
        for (const PaletteEntry& entry : e.palette) {
            const char* mark = (e.picked == entry.color) ? "←" : "";
            out(std::format("    {}{} {:5.1f}% {}", swatch(entry.color, 2), to_hex(entry.color), entry.share * 100.0f, mark));
        }
    }
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

}  // namespace wpc
