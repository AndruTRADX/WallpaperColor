// What the service needs from its memory.

#pragma once

namespace wpc {

class Memory {
public:
    virtual ~Memory() = default;
    /// Gives memory no longer used back to the system.
    virtual void release() = 0;
};

}  // namespace wpc
