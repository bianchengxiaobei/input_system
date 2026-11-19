#include "insys/state_buffers.h"

#include <algorithm>

#include "insys/device.h"
#include "insys/low/event.h"

namespace insys {

void StateBuffers::allocate_all(const std::vector<Device*>& devices) {
    free_all();

    m_offsets.resize(devices.size());
    m_sizes.resize(devices.size());

    uint32_t offset = 0;
    for (std::size_t i = 0; i < devices.size(); ++i) {
        Device* device = devices[i];
        const uint32_t size = low::align_up(device->state_size_bytes(), 8);
        offset = low::align_up(offset, 8);
        m_offsets[i] = offset;
        m_sizes[i] = size;
        offset += size;
    }

    m_totalSize = offset;
    m_front.assign(m_totalSize, 0);
    m_back.assign(m_totalSize, 0);
    defaultState.assign(m_totalSize, 0);
    noiseMask.assign(m_totalSize, 0);
    resetMask.assign(m_totalSize, 0);
}

void StateBuffers::free_all() {
    m_front.clear();
    m_back.clear();
    m_offsets.clear();
    m_sizes.clear();
    m_totalSize = 0;
    defaultState.clear();
    noiseMask.clear();
    resetMask.clear();
}

uint8_t* StateBuffers::front_for(int deviceIndex) {
    if (deviceIndex < 0 || static_cast<std::size_t>(deviceIndex) >= m_offsets.size())
        return nullptr;
    return m_front.data() + m_offsets[static_cast<std::size_t>(deviceIndex)];
}

const uint8_t* StateBuffers::front_for(int deviceIndex) const {
    if (deviceIndex < 0 || static_cast<std::size_t>(deviceIndex) >= m_offsets.size())
        return nullptr;
    return m_front.data() + m_offsets[static_cast<std::size_t>(deviceIndex)];
}

uint8_t* StateBuffers::back_for(int deviceIndex) {
    if (deviceIndex < 0 || static_cast<std::size_t>(deviceIndex) >= m_offsets.size())
        return nullptr;
    return m_back.data() + m_offsets[static_cast<std::size_t>(deviceIndex)];
}

const uint8_t* StateBuffers::back_for(int deviceIndex) const {
    if (deviceIndex < 0 || static_cast<std::size_t>(deviceIndex) >= m_offsets.size())
        return nullptr;
    return m_back.data() + m_offsets[static_cast<std::size_t>(deviceIndex)];
}

void StateBuffers::swap(int deviceIndex) {
    if (deviceIndex < 0 || static_cast<std::size_t>(deviceIndex) >= m_offsets.size())
        return;

    const std::size_t index = static_cast<std::size_t>(deviceIndex);
    const auto begin = m_front.begin() + m_offsets[index];
    const auto end = begin + m_sizes[index];
    std::copy(begin, end, m_back.begin() + m_offsets[index]);
}

} // namespace insys
