// Service: the C library's memory (glibc).

#pragma once

#include "daemon/application/contracts/memory.hpp"

namespace wpc {

class GlibcMemory final : public Memory {
public:
    /// malloc_trim: gives freed memory (a decoded picture, an unloaded driver) back to the system.
    void release() override;
};

}  // namespace wpc
