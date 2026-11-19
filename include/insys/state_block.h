#pragma once

#include <cstdint>

#include "insys/low/fourcc.h"
#include "insys/types.h"

namespace insys {

struct StateBlock {
    static constexpr uint32_t InvalidOffset = 0xFFFFFFFFu;
    static constexpr uint32_t AutomaticOffset = 0xFFFFFFFEu;

    FourCC format{};
    uint32_t byteOffset = InvalidOffset;
    uint32_t bitOffset = 0;
    uint32_t sizeInBits = 0;

    uint32_t aligned_size_in_bytes() const;
    uint32_t effective_byte_offset() const;
    uint32_t effective_bit_offset() const;
};

int32_t read_int(const StateBlock& block, const void* state);
void write_int(const StateBlock& block, void* state, int32_t value);

float read_float(const StateBlock& block, const void* state);
void write_float(const StateBlock& block, void* state, float value);

double read_double(const StateBlock& block, const void* state);
void write_double(const StateBlock& block, void* state, double value);

bool read_bit(const StateBlock& block, const void* state);
void write_bit(const StateBlock& block, void* state, bool value);

Vector2 read_vector2(const StateBlock& block, const void* state);
void write_vector2(const StateBlock& block, void* state, Vector2 value);

int32_t read_int_raw(const StateBlock& block, const void* state);
void write_int_raw(const StateBlock& block, void* state, int32_t value);

} // namespace insys

