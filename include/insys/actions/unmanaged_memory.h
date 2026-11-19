#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace insys {

class UnmanagedMemory {
public:
    UnmanagedMemory() = default;

    void allocate(std::size_t bytes);
    void release();

    uint8_t* data() { return m_data.get(); }
    const uint8_t* data() const { return m_data.get(); }
    std::size_t size() const { return m_size; }
    bool allocated() const { return m_data != nullptr; }

    template <typename T>
    T* at(std::size_t byteOffset) {
        return reinterpret_cast<T*>(m_data.get() + byteOffset);
    }

    template <typename T>
    const T* at(std::size_t byteOffset) const {
        return reinterpret_cast<const T*>(m_data.get() + byteOffset);
    }

private:
    std::unique_ptr<uint8_t[]> m_data;
    std::size_t m_size = 0;
};

inline std::size_t align_up_size(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace insys
