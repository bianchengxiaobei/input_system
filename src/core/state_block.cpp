#include "insys/state_block.h"

#include <cstring>

#include "bit_math.h"
#include "insys/low/state_format.h"

namespace insys {

uint32_t StateBlock::aligned_size_in_bytes() const {
    return (sizeInBits + 7u) / 8u;
}

uint32_t StateBlock::effective_byte_offset() const {
    return effective_bit_offset() / 8u;
}

uint32_t StateBlock::effective_bit_offset() const {
    if (byteOffset == AutomaticOffset)
        return bitOffset;
    if (byteOffset == InvalidOffset)
        return 0;
    return byteOffset * 8u + bitOffset;
}

namespace {

const uint8_t* base(const void* state) {
    return static_cast<const uint8_t*>(state);
}

uint8_t* base(void* state) {
    return static_cast<uint8_t*>(state);
}

} // namespace

int32_t read_int(const StateBlock& block, const void* state) {
    const uint32_t bit = block.effective_bit_offset();
    if (block.format == kFormatBit)
        return detail::read_single_bit(base(state), bit) ? 1 : 0;
    if (block.format == kFormatByte) {
        int32_t value = 0;
        std::memcpy(&value, base(state) + block.effective_byte_offset(), sizeof(uint8_t));
        return value;
    }
    if (block.format == kFormatUShort) {
        uint16_t value = 0;
        std::memcpy(&value, base(state) + block.effective_byte_offset(), sizeof(uint16_t));
        return static_cast<int32_t>(value);
    }
    if (block.format == kFormatUInt) {
        uint32_t value = 0;
        std::memcpy(&value, base(state) + block.effective_byte_offset(), sizeof(uint32_t));
        return static_cast<int32_t>(value);
    }
    if (block.format == kFormatInt) {
        if (block.sizeInBits <= 8) {
            int8_t value = 0;
            std::memcpy(&value, base(state) + block.effective_byte_offset(), sizeof(int8_t));
            return value;
        }
        if (block.sizeInBits <= 16) {
            int16_t value = 0;
            std::memcpy(&value, base(state) + block.effective_byte_offset(), sizeof(int16_t));
            return value;
        }
        int32_t value = 0;
        std::memcpy(&value, base(state) + block.effective_byte_offset(), sizeof(int32_t));
        return value;
    }
    if (block.format == kFormatFloat)
        return static_cast<int32_t>(read_float(block, state));
    return 0;
}

void write_int(const StateBlock& block, void* state, int32_t value) {
    const uint32_t bit = block.effective_bit_offset();
    if (block.format == kFormatBit) {
        detail::write_single_bit(base(state), bit, value != 0);
        return;
    }
    if (block.format == kFormatByte) {
        const uint8_t byte = static_cast<uint8_t>(value);
        std::memcpy(base(state) + block.effective_byte_offset(), &byte, sizeof(uint8_t));
        return;
    }
    if (block.format == kFormatUShort) {
        const uint16_t shortValue = static_cast<uint16_t>(value);
        std::memcpy(base(state) + block.effective_byte_offset(), &shortValue,
                    sizeof(uint16_t));
        return;
    }
    if (block.format == kFormatUInt) {
        const uint32_t intValue = static_cast<uint32_t>(value);
        std::memcpy(base(state) + block.effective_byte_offset(), &intValue,
                    sizeof(uint32_t));
        return;
    }
    if (block.format == kFormatInt) {
        if (block.sizeInBits <= 8) {
            const int8_t shortValue = static_cast<int8_t>(value);
            std::memcpy(base(state) + block.effective_byte_offset(), &shortValue,
                        sizeof(int8_t));
            return;
        }
        if (block.sizeInBits <= 16) {
            const int16_t shortValue = static_cast<int16_t>(value);
            std::memcpy(base(state) + block.effective_byte_offset(), &shortValue,
                        sizeof(int16_t));
            return;
        }
        std::memcpy(base(state) + block.effective_byte_offset(), &value, sizeof(int32_t));
        return;
    }
    if (block.format == kFormatFloat)
        write_float(block, state, static_cast<float>(value));
}

float read_float(const StateBlock& block, const void* state) {
    if (block.format == kFormatFloat) {
        float value = 0.0f;
        std::memcpy(&value, base(state) + block.effective_byte_offset(), sizeof(float));
        return value;
    }
    return static_cast<float>(read_int(block, state));
}

void write_float(const StateBlock& block, void* state, float value) {
    if (block.format == kFormatFloat) {
        std::memcpy(base(state) + block.effective_byte_offset(), &value, sizeof(float));
        return;
    }
    write_int(block, state, static_cast<int32_t>(value));
}

double read_double(const StateBlock& block, const void* state) {
    if (block.format == kFormatDouble) {
        double value = 0.0;
        std::memcpy(&value, base(state) + block.effective_byte_offset(), sizeof(double));
        return value;
    }
    return static_cast<double>(read_float(block, state));
}

void write_double(const StateBlock& block, void* state, double value) {
    if (block.format == kFormatDouble) {
        std::memcpy(base(state) + block.effective_byte_offset(), &value, sizeof(double));
        return;
    }
    write_float(block, state, static_cast<float>(value));
}

bool read_bit(const StateBlock& block, const void* state) {
    if (block.format == kFormatBit)
        return detail::read_single_bit(base(state), block.effective_bit_offset());
    return read_int(block, state) != 0;
}

void write_bit(const StateBlock& block, void* state, bool value) {
    if (block.format == kFormatBit) {
        detail::write_single_bit(base(state), block.effective_bit_offset(), value);
        return;
    }
    write_int(block, state, value ? 1 : 0);
}

Vector2 read_vector2(const StateBlock& block, const void* state) {
    if (block.format != kFormatVector2)
        return {};

    const uint8_t* address = base(state) + block.effective_byte_offset();
    Vector2 value;
    std::memcpy(&value, address, sizeof(Vector2));
    return value;
}

void write_vector2(const StateBlock& block, void* state, Vector2 value) {
    if (block.format != kFormatVector2)
        return;
    std::memcpy(base(state) + block.effective_byte_offset(), &value, sizeof(Vector2));
}

int32_t read_int_raw(const StateBlock& block, const void* state) {
    return read_int(block, state);
}

void write_int_raw(const StateBlock& block, void* state, int32_t value) {
    write_int(block, state, value);
}

} // namespace insys



