#pragma once

#include <algorithm>
#include <cstdint>

#include "insys/low/fourcc.h"

namespace insys {

inline constexpr FourCC kGetHapticCapabilitiesCommand = FourCC('G', 'H', 'C', ' ');
inline constexpr FourCC kSendHapticImpulseCommand = FourCC('H', 'I', 'M', 'P');
inline constexpr FourCC kSendBufferedHapticsCommand = FourCC('H', 'B', 'U', 'F');
inline constexpr FourCC kSetLightBarColorCommand = FourCC('L', 'B', 'C', ' ');
inline constexpr FourCC kSetAdaptiveTriggerCommand = FourCC('A', 'T', 'R', 'G');
inline constexpr FourCC kSendSwitchSubcommandCommand = FourCC('S', 'W', 'S', 'C');
inline constexpr FourCC kSendHidOutputReportCommand = FourCC('H', 'O', 'U', 'T');
inline constexpr FourCC kSendHidFeatureReportCommand = FourCC('H', 'F', 'W', 'T');
inline constexpr FourCC kGetHidFeatureReportCommand = FourCC('H', 'F', 'R', 'D');

struct HapticCapabilities {
    bool supportsLowFrequency = false;
    bool supportsHighFrequency = false;
    bool supportsLeftTrigger = false;
    bool supportsRightTrigger = false;
    bool supportsLightBar = false;
    uint32_t maxBufferSize = 0;
};

struct GetHapticCapabilitiesCommand {
    HapticCapabilities* capabilities = nullptr;
};

struct SendHapticImpulseCommand {
    enum class Motor : int {
        LowFrequency = 0,
        HighFrequency = 1,
        LeftTrigger = 2,
        RightTrigger = 3,
    };

    int motorChannel = static_cast<int>(Motor::LowFrequency);
    float amplitude = 0.0f;
    float duration = 0.0f;
};

struct SendBufferedHapticsCommand {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
};

struct SetLightBarColorCommand {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
};

struct AdaptiveTriggerEffect {
    uint8_t mode = 0x05;
    uint8_t parameters[10] = {};

    static uint8_t to_byte(float value) {
        return static_cast<uint8_t>(
            std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    }

    static AdaptiveTriggerEffect off() {
        return {};
    }

    static AdaptiveTriggerEffect feedback(float startPosition,
                                          float strength) {
        AdaptiveTriggerEffect effect;
        effect.mode = 0x21;
        effect.parameters[0] = to_byte(startPosition);
        effect.parameters[1] = to_byte(strength);
        return effect;
    }

    static AdaptiveTriggerEffect weapon(float startPosition, float endPosition,
                                        float strength) {
        AdaptiveTriggerEffect effect;
        effect.mode = 0x25;
        effect.parameters[0] = to_byte(startPosition);
        effect.parameters[1] = to_byte(endPosition);
        effect.parameters[2] = to_byte(strength);
        return effect;
    }

    static AdaptiveTriggerEffect vibration(float startPosition, float amplitude,
                                           float frequency) {
        AdaptiveTriggerEffect effect;
        effect.mode = 0x26;
        effect.parameters[0] = to_byte(startPosition);
        effect.parameters[1] = to_byte(amplitude);
        effect.parameters[2] = to_byte(frequency);
        return effect;
    }

    static AdaptiveTriggerEffect bow(float startPosition, float endPosition,
                                     float strength, float snapForce) {
        AdaptiveTriggerEffect effect;
        effect.mode = 0x22;
        effect.parameters[0] = to_byte(startPosition);
        effect.parameters[1] = to_byte(endPosition);
        effect.parameters[2] = to_byte(strength);
        effect.parameters[3] = to_byte(snapForce);
        return effect;
    }
};

struct SetAdaptiveTriggerCommand {
    enum class Trigger : int {
        Left = 0,
        Right = 1,
    };

    Trigger trigger = Trigger::Right;
    AdaptiveTriggerEffect effect;
};

struct SendSwitchSubcommandCommand {
    uint8_t subcommand = 0;
    const uint8_t* data = nullptr;
    uint32_t size = 0;
};

struct SendHidReportCommand {
    uint8_t reportId = 0;
    bool prependReportId = false;
    const uint8_t* data = nullptr;
    uint32_t size = 0;
};

struct GetHidFeatureReportCommand {
    uint8_t reportId = 0;
    uint8_t* data = nullptr;
    uint32_t capacity = 0;
    uint32_t* reportSize = nullptr;
};

} // namespace insys



