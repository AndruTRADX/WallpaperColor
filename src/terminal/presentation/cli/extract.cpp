// `extract`: the colours that would be taken from pictures.

#include <cstdlib>
#include <format>
#include <iostream>

#include "terminal/application/features/extract_colours/extract_colours.hpp"
#include "terminal/presentation/cli/commands.hpp"
#include "terminal/presentation/cli/output.hpp"

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
        out(std::format("{}{}  arc {}{} {} {}  {}", swatch(e.led, 6), e.led.hex(), arc, e.arc[0].hex(), e.arc[1].hex(),
                        e.arc[2].hex(), p.picture.string()));
        if (!palette) continue;
        for (const PaletteEntry& entry : e.palette) {
            const char* mark = (e.picked == entry.color) ? "←" : "";
            out(std::format("    {}{} {:5.1f}% {}", swatch(entry.color, 2), entry.color.hex(), entry.share * 100.0f, mark));
        }
    }
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

}  // namespace wpc
