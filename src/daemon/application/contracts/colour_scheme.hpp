// What the service needs to know about the desktop's colours.

#pragma once

namespace wpc {

class ColourScheme {
public:
    virtual ~ColourScheme() = default;
    /// Whether the desktop's colour scheme is dark.
    virtual bool is_dark() = 0;
};

}  // namespace wpc
