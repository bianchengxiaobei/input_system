#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "insys/backend/runtime.h"
#include "insys/device_commands.h"
#include "insys/hid/hid_gamepad.h"

namespace insys {
class Manager;
}

namespace insys::backend {

enum class HidErrorCode {
    DeviceOpenFailed,
    PermissionDenied,
    ReadFailed,
    WriteFailed,
    FeatureReadFailed,
    ReportQueueOverflow,
    DuplicateDevice,
    InvalidReport,
};

struct HidError {
    HidErrorCode code = HidErrorCode::InvalidReport;
    std::string deviceId;
    std::string message;
};

enum class HidReportKind {
    Input,
    Output,
    Feature,
};

struct HidReport {
    std::string deviceId;
    HidReportKind kind = HidReportKind::Input;
    std::vector<uint8_t> data;
    double time = 0.0;
};

struct DeviceView {
    std::string id;
    hid::DeviceInfo info;
    hid::DeviceKind kind = hid::DeviceKind::Generic;
    std::string layoutName;
};

class HidBackend {
public:
    using OutputReportHandler =
        std::function<bool(std::string_view id, const uint8_t* data,
                           std::size_t size)>;
    using FeatureReportHandler =
        std::function<bool(std::string_view id, const uint8_t* data,
                           std::size_t size)>;
    using FeatureReportReader =
        std::function<int(std::string_view id, uint8_t reportId,
                          uint8_t* data, std::size_t size)>;
    using ErrorHandler = std::function<void(const HidError& error)>;
    using ReportListener = std::function<void(const HidReport& report)>;

    HidBackend() = default;
    ~HidBackend();

    HidBackend(const HidBackend&) = delete;
    HidBackend& operator=(const HidBackend&) = delete;

    bool initialize(Runtime& runtime, Manager& manager);
    void shutdown();

    bool add_device(hid::DeviceInfo info);
    bool remove_device(std::string_view id);
    bool feed_report(std::string_view id, const uint8_t* data, std::size_t size,
                     double time = -1.0);
    void poll();
    void poll(double time);

    bool connected(std::string_view id) const;
    int device_id(std::string_view id) const;
    hid::DeviceKind kind(std::string_view id) const;
    hid::HidTransport transport(std::string_view id) const;
    std::vector<DeviceView> devices() const;
    std::vector<HidError> recent_errors() const;
    void clear_recent_errors();

    void set_output_report_handler(OutputReportHandler handler);
    void set_feature_report_handler(FeatureReportHandler handler);
    void set_feature_report_reader(FeatureReportReader reader);
    void set_error_handler(ErrorHandler handler);
    void set_report_listener(ReportListener listener);
    bool send_output_report(std::string_view id, const uint8_t* data,
                            std::size_t size);
    bool send_feature_report(std::string_view id, const uint8_t* data,
                             std::size_t size);
    int read_feature_report(std::string_view id, uint8_t reportId,
                            uint8_t* data, std::size_t size);
    void report_error(HidErrorCode code, std::string_view id,
                      std::string message);
    void notify_report(std::string_view id, HidReportKind kind,
                       const uint8_t* data, std::size_t size, double time);

private:
    struct GenericReportState {
        uint32_t stateOffset = 0;
        uint32_t stateSize = 0;
        std::vector<uint8_t> rawState;
        uint8_t reportId = 1;
        bool hasReportId = false;
        bool dirty = false;
        double reportTime = -1.0;
    };

    struct Slot {
        std::string id;
        std::string dedupKey;
        std::string layoutName;
        hid::DeviceInfo info;
        int deviceId = 0;
        hid::DeviceKind kind = hid::DeviceKind::Generic;
        hid::HidTransport transport = hid::HidTransport::Unknown;
        FourCC stateFormat{};
        uint32_t stateSize = 0;
        hid::HidGamepadState gamepad;
        std::vector<GenericReportState> genericReports;
        std::vector<uint8_t> genericState;
        uint32_t genericStateSize = 0;
        bool reportDataIncludesId = true;
        bool dirty = false;
        double reportTime = -1.0;
        uint32_t outputReportSize = 0;
        uint32_t featureReportSize = 0;
        bool bluetooth = false;
        bool bluetoothKnown = false;
        uint8_t outputSequence = 0;
        float lowFrequency = 0.0f;
        float highFrequency = 0.0f;
        float lightBar[3] = {0.0f, 0.0f, 0.0f};
        bool hasLightBar = false;
        bool hasLeftTriggerEffect = false;
        bool hasRightTriggerEffect = false;
        uint8_t leftTriggerEffect[11] = {0x05};
        uint8_t rightTriggerEffect[11] = {0x05};
        uint8_t switchPacketCounter = 0;
        int switchHandshakeStep = 0;
        double switchHandshakeTime = 0.0;
    };

    void remove_slot_devices(Slot& slot);
    void queue_state(Slot& slot, double time);
    void queue_generic_state(Slot& slot, double time);
    Slot* find_slot(std::string_view id);
    const Slot* find_slot(std::string_view id) const;
    Slot* find_slot_by_device(int deviceId);
    long handle_device_command(int deviceId, DeviceCommand* command);
    bool send_haptic_impulse(Slot& slot, const SendHapticImpulseCommand& request);
    bool emit_output(Slot& slot, const float* lightBarColor);
    void update_switch_handshake(Slot& slot, double time);

    Runtime* m_runtime = nullptr;
    Manager* m_manager = nullptr;
    std::unordered_map<std::string, Slot> m_slots;
    OutputReportHandler m_outputHandler;
    FeatureReportHandler m_featureHandler;
    FeatureReportReader m_featureReader;
    ErrorHandler m_errorHandler;
    ReportListener m_reportListener;
    std::vector<HidError> m_recentErrors;
    int m_commandHandlerId = 0;
};

} // namespace insys::backend












