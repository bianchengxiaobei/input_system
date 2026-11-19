#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "insys/backend/hid_backend.h"
#include "insys/device.h"
#include "insys/device_commands.h"
#include "insys/hid/hid_descriptor.h"

namespace insys::editor {

class PreviewRuntime;

struct HidControlView {
    std::string path;
    std::string name;
    std::string layout;
    uint32_t byteOffset = 0;
    uint32_t bitOffset = 0;
    uint32_t sizeInBits = 0;
    bool isButton = false;
    bool isAxis = false;
    float value = 0.0f;
};

struct HidDescriptorView {
    bool parsed = false;
    std::string error;
    hid::DeviceDescriptor descriptor;
    std::vector<HidControlView> controls;
};

struct HidMonitorReport {
    std::uint64_t sequence = 0;
    std::string deviceId;
    backend::HidReportKind kind = backend::HidReportKind::Input;
    std::vector<uint8_t> data;
    double time = 0.0;
};

struct HidToolError {
    backend::HidErrorCode code = backend::HidErrorCode::InvalidReport;
    std::string deviceId;
    std::string message;
    double time = 0.0;
};

class HidToolService {
public:
    explicit HidToolService(PreviewRuntime& preview,
                            std::size_t maxReportsPerMonitor = 500);
    ~HidToolService();

    HidToolService(const HidToolService&) = delete;
    HidToolService& operator=(const HidToolService&) = delete;

    void shutdown();

    std::vector<backend::DeviceView> devices();
    std::optional<backend::DeviceView> device(std::string_view deviceId);
    std::optional<HidDescriptorView> descriptor(std::string_view deviceId,
                                                std::string& error);

    std::string start_monitor(std::string deviceId,
                              std::vector<backend::HidReportKind> types,
                              std::string& error);
    bool has_monitor(std::string_view monitorId) const;
    bool stop_monitor(std::string_view monitorId);
    bool pause_monitor(std::string_view monitorId, bool paused);
    bool clear_monitor(std::string_view monitorId);
    std::vector<HidMonitorReport> poll_monitor(std::string_view monitorId,
                                               std::size_t maxReports,
                                               std::size_t& dropped,
                                               bool& paused);

    bool read_feature(std::string_view deviceId, uint8_t reportId,
                      std::size_t size, std::vector<uint8_t>& bytes,
                      std::string& error);
    bool write_feature(std::string_view deviceId,
                       const std::vector<uint8_t>& bytes,
                       std::string& error);
    bool write_output(std::string_view deviceId,
                      const std::vector<uint8_t>& bytes,
                      std::string& error);
    bool haptics(std::string_view deviceId, float left, float right,
                 std::string& error);
    bool lightbar(std::string_view deviceId, float red, float green,
                  float blue, std::string& error);
    bool adaptive_trigger(std::string_view deviceId, bool left, uint8_t mode,
                          const std::vector<uint8_t>& parameters,
                          std::string& error);
    bool switch_subcommand(std::string_view deviceId, uint8_t subcommand,
                           const std::vector<uint8_t>& payload,
                           std::string& error);

    std::vector<HidToolError> errors(std::size_t maxErrors = 100);
    void clear_errors();

private:
    struct Monitor {
        std::string id;
        std::string deviceId;
        std::vector<backend::HidReportKind> types;
        std::deque<HidMonitorReport> reports;
        std::size_t dropped = 0;
        std::size_t maxReports = 0;
        std::uint64_t nextSequence = 0;
        bool paused = false;
    };

    backend::HidBackend* backend();
    void prepare();
    void attach();
    void on_report(const backend::HidReport& report);
    void on_error(const backend::HidError& error);
    Device* find_device(std::string_view deviceId);
    Monitor* find_monitor(std::string_view monitorId);
    bool write_command(std::string_view deviceId, DeviceCommand& command,
                       std::string& error);
    void add_error(backend::HidErrorCode code, std::string_view deviceId,
                   std::string message);

    PreviewRuntime& m_preview;
    std::size_t m_maxReportsPerMonitor = 500;
    std::size_t m_nextMonitorId = 0;
    std::unordered_map<std::string, Monitor> m_monitors;
    std::deque<HidToolError> m_errors;
    backend::HidBackend* m_attached = nullptr;
};

} // namespace insys::editor
