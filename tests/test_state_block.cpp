#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "core/bit_math.h"
#include "insys/low/state_format.h"
#include "insys/state_block.h"

using namespace insys;

TEST_CASE("Single bit reads and writes across byte boundaries") {
    uint8_t data[2] = {0, 0};

    detail::write_single_bit(data, 6, true);
    REQUIRE(detail::read_single_bit(data, 6));
    REQUIRE(data[0] == 0x40);

    detail::write_single_bit(data, 9, true);
    REQUIRE(detail::read_single_bit(data, 9));
    REQUIRE(data[1] == 0x02);

    detail::write_single_bit(data, 6, false);
    REQUIRE_FALSE(detail::read_single_bit(data, 6));
}

TEST_CASE("Multiple bits can straddle a byte boundary") {
    uint8_t data[2] = {0, 0};
    detail::write_multiple_bits(data, 6, 4, 0b1011u);
    REQUIRE(detail::read_multiple_bits(data, 6, 4) == 0b1011u);

    detail::write_multiple_bits(data, 6, 4, 0u);
    REQUIRE(detail::read_multiple_bits(data, 6, 4) == 0u);
}

TEST_CASE("Normalized bits clamp to [0, 1]") {
    uint8_t data[2] = {0, 0};
    detail::write_normalized(data, 0, 8, 0.5f);
    REQUIRE(detail::read_normalized(data, 0, 8) > 0.49f);
    REQUIRE(detail::read_normalized(data, 0, 8) < 0.51f);

    detail::write_normalized(data, 0, 8, -1.0f);
    REQUIRE(detail::read_normalized(data, 0, 8) == 0.0f);

    detail::write_normalized(data, 0, 8, 2.0f);
    REQUIRE(detail::read_normalized(data, 0, 8) == 1.0f);
}

TEST_CASE("Float StateBlock round-trips") {
    alignas(8) uint8_t state[16]{};
    StateBlock block;
    block.format = kFormatFloat;
    block.byteOffset = 4;
    block.sizeInBits = 32;

    write_float(block, state, 12.5f);
    REQUIRE(read_float(block, state) == 12.5f);
    REQUIRE(read_int(block, state) == 12);
}

TEST_CASE("Int and byte StateBlock round-trip") {
    alignas(8) uint8_t state[16]{};

    StateBlock intBlock;
    intBlock.format = kFormatInt;
    intBlock.byteOffset = 8;
    intBlock.sizeInBits = 32;
    write_int(intBlock, state, -12345);
    REQUIRE(read_int(intBlock, state) == -12345);

    StateBlock byteBlock;
    byteBlock.format = kFormatByte;
    byteBlock.byteOffset = 2;
    byteBlock.sizeInBits = 8;
    write_int(byteBlock, state, 200);
    REQUIRE(read_int(byteBlock, state) == 200);
}

TEST_CASE("Bit StateBlock round-trips") {
    alignas(8) uint8_t state[8]{};

    StateBlock block;
    block.format = kFormatBit;
    block.byteOffset = 0;
    block.bitOffset = 6;
    block.sizeInBits = 1;

    write_bit(block, state, true);
    REQUIRE(read_bit(block, state));
    REQUIRE(read_float(block, state) == 1.0f);
    write_bit(block, state, false);
    REQUIRE_FALSE(read_bit(block, state));
}

TEST_CASE("Vector2 StateBlock round-trips") {
    alignas(8) uint8_t state[16]{};

    StateBlock block;
    block.format = kFormatVector2;
    block.byteOffset = 8;
    block.sizeInBits = 64;

    write_vector2(block, state, Vector2{3.0f, -4.0f});
    const Vector2 value = read_vector2(block, state);
    REQUIRE(value.x == 3.0f);
    REQUIRE(value.y == -4.0f);
}

TEST_CASE("StateBlock sizes and offsets") {
    StateBlock block;
    block.byteOffset = StateBlock::AutomaticOffset;
    block.bitOffset = 11;
    block.sizeInBits = 5;
    REQUIRE(block.aligned_size_in_bytes() == 1);
    REQUIRE(block.effective_bit_offset() == 11);

    block.byteOffset = 4;
    block.bitOffset = 3;
    block.sizeInBits = 9;
    REQUIRE(block.effective_bit_offset() == 35);
    REQUIRE(block.effective_byte_offset() == 4);
    REQUIRE(block.aligned_size_in_bytes() == 2);
}
