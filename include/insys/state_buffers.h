#pragma once

#include <cstdint>
#include <vector>

#include "insys/update_type.h"

namespace insys {

class Device;

class StateBuffers {
public:
    struct DoubleBuffers {
        std::vector<uint8_t> front;
        std::vector<uint8_t> back;
    };

    void allocate_all(const std::vector<Device*>& devices);
    void free_all();

    void switch_to(UpdateType type) { m_current = type; }
    UpdateType current_update() const { return m_current; }

    uint8_t* front_for(int deviceIndex);
    const uint8_t* front_for(int deviceIndex) const;
    uint8_t* back_for(int deviceIndex);
    const uint8_t* back_for(int deviceIndex) const;

    void swap(int deviceIndex);

    uint32_t total_size() const { return m_totalSize; }

    std::vector<uint8_t> defaultState;
    std::vector<uint8_t> noiseMask;
    std::vector<uint8_t> resetMask;

private:
    std::vector<uint8_t> m_front;
    std::vector<uint8_t> m_back;
    std::vector<uint32_t> m_offsets;
    std::vector<uint32_t> m_sizes;
    uint32_t m_totalSize = 0;
    UpdateType m_current = UpdateType::None;
};

} // namespace insys
