#include "insys/actions/unmanaged_memory.h"

namespace insys {

void UnmanagedMemory::allocate(std::size_t bytes) {
    release();
    if (bytes == 0)
        return;
    m_data = std::make_unique<uint8_t[]>(bytes);
    m_size = bytes;
}

void UnmanagedMemory::release() {
    m_data.reset();
    m_size = 0;
}

} // namespace insys
