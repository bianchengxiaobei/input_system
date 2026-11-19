#pragma once

#include <cstdint>

#include "insys/device_states.h"
#include "insys/low/key.h"

namespace insys::backend::evdev {

inline constexpr uint16_t kEvSyn = 0x00;
inline constexpr uint16_t kEvKey = 0x01;
inline constexpr uint16_t kEvRel = 0x02;
inline constexpr uint16_t kEvAbs = 0x03;

inline constexpr uint16_t kSynReport = 0;
inline constexpr uint16_t kRelX = 0;
inline constexpr uint16_t kRelY = 1;
inline constexpr uint16_t kRelHWheel = 6;
inline constexpr uint16_t kRelWheel = 8;

inline constexpr uint16_t kAbsX = 0;
inline constexpr uint16_t kAbsY = 1;
inline constexpr uint16_t kAbsZ = 2;
inline constexpr uint16_t kAbsRX = 3;
inline constexpr uint16_t kAbsRY = 4;
inline constexpr uint16_t kAbsRZ = 5;
inline constexpr uint16_t kAbsHat0X = 0x10;
inline constexpr uint16_t kAbsHat0Y = 0x11;

inline constexpr uint16_t kBtnLeft = 0x110;
inline constexpr uint16_t kBtnRight = 0x111;
inline constexpr uint16_t kBtnMiddle = 0x112;
inline constexpr uint16_t kBtnSide = 0x113;
inline constexpr uint16_t kBtnExtra = 0x114;
inline constexpr uint16_t kBtnSouth = 0x130;
inline constexpr uint16_t kBtnEast = 0x131;
inline constexpr uint16_t kBtnNorth = 0x133;
inline constexpr uint16_t kBtnWest = 0x134;
inline constexpr uint16_t kBtnTL = 0x136;
inline constexpr uint16_t kBtnTR = 0x137;
inline constexpr uint16_t kBtnTL2 = 0x138;
inline constexpr uint16_t kBtnTR2 = 0x139;
inline constexpr uint16_t kBtnSelect = 0x13a;
inline constexpr uint16_t kBtnStart = 0x13b;
inline constexpr uint16_t kBtnThumbL = 0x13d;
inline constexpr uint16_t kBtnThumbR = 0x13e;
inline constexpr uint16_t kBtnDpadUp = 0x220;
inline constexpr uint16_t kBtnDpadDown = 0x221;
inline constexpr uint16_t kBtnDpadLeft = 0x222;
inline constexpr uint16_t kBtnDpadRight = 0x223;

bool map_key(uint16_t code, Key& key);

struct MouseMapper {
    MouseState state{};

    bool process(uint16_t type, uint16_t code, int32_t value);
    void reset() { state = {}; }
};

struct GamepadMapper {
    GamepadState state{};
    float axisMin = -32768.0f;
    float axisMax = 32767.0f;
    float triggerMin = 0.0f;
    float triggerMax = 255.0f;

    bool process(uint16_t type, uint16_t code, int32_t value);
    void reset() { state = {}; }
};

} // namespace insys::backend::evdev


