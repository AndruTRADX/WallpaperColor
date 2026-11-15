// Giving freed memory back to the system.

#pragma once

#include <malloc.h>

namespace wpc {

/// glibc keeps freed memory for later use; this returns what it can to the system
/// (malloc_trim). For right after a big temporary allocation: a decoded picture, an unloaded
/// driver. Costs well under a millisecond.
inline void release_free_memory() {
    ::malloc_trim(0);
}

}  // namespace wpc
