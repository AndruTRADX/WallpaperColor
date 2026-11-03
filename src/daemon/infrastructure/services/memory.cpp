#include "daemon/infrastructure/services/memory.hpp"

#include <malloc.h>

namespace wpc {

void GlibcMemory::release() {
    malloc_trim(0);
}

}  // namespace wpc
