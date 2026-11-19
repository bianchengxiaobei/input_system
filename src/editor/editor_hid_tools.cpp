#include "insys/editor/editor_hid_tools.h"

#include <algorithm>
#include <utility>

#include "insys/control.h"
#include "insys/control_path.h"
#include "insys/editor/editor_preview.h"
#include "insys/manager.h"

namespace insys::editor {

HidToolService::HidToolService(PreviewRuntime& preview,
                               std::size_t maxReportsPerMonitor)
    : m_preview(preview), m_maxReportsPerMonitor(maxReportsPerMonitor) {}

HidToolService::~HidToolService() {
    shutdown();
}

void HidToolService::shutdown() {
    m_monitors.clear();
    if (backend::HidBackend* hid = backend()) {
        hid->set_report_listener({});
        hid->set_error_handler({});
    }
    m_attached = nullptr;
}

backend::HidBackend* HidToolService::backend() {
    return m_preview.hid_backend();
}

void HidToolService::prepare() {
    if (backend() == nullptr)
        m_attached = nullptr;
    m_preview.ensure_hid();
    attach();
    m_preview.poll_hid();
}

void HidToolService::attach() {
    backend::HidBackend* hid = backend();
    if (hid == nullptr) {
        m_attached = nullptr;
        return;
    }
    if (hid != m_attached) {
        for (const backend::HidError& error : hid->recent_errors())
            on_error(error);
        m_attached = hid;
    }
    if (m_monitors.empty()) {
        hid->set_report_listener({});
    } else {
        hid->set_report_listener(
            [this](const backend::HidReport& report) { on_report(report); });
    }
    hid->set_error_handler(
        [this](const backend::HidError& error) { on_error(error); });
}

std::vector<backend::DeviceView> HidToolService::devices() {
    prepare();
    backend::HidBackend* hid = backend();
    return hid == nullptr ? std::vector<backend::DeviceView>{} : hid->devices();
}

std::optional<backend::DeviceView> HidToolService::device(
    std::string_view deviceId) {
    for (const backend::DeviceView& view : devices()) {
        if (view.id == deviceId)
            return view;
    }
    return std::nullopt;
}

std::optional<HidDescriptorView> HidToolService::descriptor(
    std::string_view deviceId, std::string& error) {
    const std::optional<backend::DeviceView> view = device(deviceId);
    if (!view.has_value()) {
        error = "HID device not found: " + std::string(deviceId);
        return std::nullopt;
    }

    HidDescriptorView result;
    if (view->info.reportDescriptor.empty()) {
        result.error = "device did not provide a HID report descriptor";
    } else if (!hid::parse_report_descriptor(view->info.reportDescriptor.data(),
                                             view->info.reportDescriptor.size(),
                                             result.descriptor)) {
        result.error = "failed to parse HID report descriptor";
    } else {
        result.parsed = true;
    }

    if (Device* device = find_device(deviceId)) {
        const std::uint8_t* state = device->current_state();
        for (Control* control : device->allControls) {
            if (control == nullptr || control == device)
                continue;

            HidControlView item;
            item.path = control_path(*control);
            item.name = control->name.str();
            item.layout = control->layout.str();
            item.byteOffset = control->stateBlock.effective_byte_offset();
            item.bitOffset = control->stateBlock.effective_bit_offset();
            item.sizeInBits = control->stateBlock.sizeInBits;
            item.isButton = control->is_button();
            item.isAxis = control->is_axis();
            if (state != nullptr && item.isAxis) {
                if (const auto* vector =
                        dynamic_cast<const Vector2Control*>(control)) {
                    item.value = vector->read_vector2(state).length();
                } else if (const auto* vector3 =
                               dynamic_cast<const Vector3Control*>(control)) {
                    item.value = vector3->read_vector3(state).length();
                } else if (control->value_size_bytes() == sizeof(float)) {
                    item.value = control->read_float(state);
                }
            }
            result.controls.push_back(std::move(item));
        }
    }

    return result;
}

std::string HidToolService::start_monitor(
    std::string deviceId, std::vector<backend::HidReportKind> types,
    std::string& error) {
    if (deviceId.empty()) {
        error = "deviceId is required";
        return {};
    }
    if (types.empty())
        types.push_back(backend::HidReportKind::Input);
    if (!device(deviceId).has_value()) {
        error = "HID device not found: " + deviceId;
        return {};
    }

    Monitor monitor;
    monitor.deviceId = std::move(deviceId);
    monitor.types = std::move(types);
    monitor.maxReports =
        m_maxReportsPerMonitor == 0 ? 500 : m_maxReportsPerMonitor;
    monitor.id = "hidmon-" + std::to_string(++m_nextMonitorId);
    const std::string id = monitor.id;
    m_monitors.emplace(id, std::move(monitor));
    attach();
    return id;
}

bool HidToolService::has_monitor(std::string_view monitorId) const {
    return m_monitors.find(std::string(monitorId)) != m_monitors.end();
}

bool HidToolService::stop_monitor(std::string_view monitorId) {
    const bool removed = m_monitors.erase(std::string(monitorId)) != 0;
    if (removed && m_monitors.empty()) {
        if (backend::HidBackend* hid = backend())
            hid->set_report_listener({});
    }
    return removed;
}

bool HidToolService::pause_monitor(std::string_view monitorId, bool paused) {
    Monitor* monitor = find_monitor(monitorId);
    if (monitor == nullptr)
        return false;
    monitor->paused = paused;
    return true;
}

bool HidToolService::clear_monitor(std::string_view monitorId) {
    Monitor* monitor = find_monitor(monitorId);
    if (monitor == nullptr)
        return false;
    monitor->reports.clear();
    monitor->dropped = 0;
    return true;
}

std::vector<HidMonitorReport> HidToolService::poll_monitor(
    std::string_view monitorId, std::size_t maxReports, std::size_t& dropped,
    bool& paused) {
    Monitor* monitor = find_monitor(monitorId);
    if (monitor == nullptr)
        return {};

    attach();
    m_preview.poll_hid();

    dropped = monitor->dropped;
    monitor->dropped = 0;
    paused = monitor->paused;
    const std::size_t count =
        maxReports == 0 ? monitor->reports.size()
                        : std::min(maxReports, monitor->reports.size());
    std::vector<HidMonitorReport> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        result.push_back(std::move(monitor->reports.front()));
        monitor->reports.pop_front();
    }
    return result;
}

bool HidToolService::read_feature(std::string_view deviceId, uint8_t reportId,
                                  std::size_t size,
                                  std::vector<uint8_t>& bytes,
                                  std::string& error) {
    prepare();
    backend::HidBackend* hid = backend();
    if (hid == nullptr) {
        error = "HID backend is not running";
        return false;
    }
    if (size == 0) {
        error = "feature report size must be positive";
        return false;
    }
    bytes.assign(size, 0);
    const int read = hid->read_feature_report(
        deviceId, reportId, bytes.data(), bytes.size());
    if (read < 0) {
        error = "HID feature report read failed";
        bytes.clear();
        return false;
    }
    bytes.resize(static_cast<std::size_t>(read));
    return true;
}

bool HidToolService::write_feature(std::string_view deviceId,
                                   const std::vector<uint8_t>& bytes,
                                   std::string& error) {
    prepare();
    backend::HidBackend* hid = backend();
    if (hid == nullptr) {
        error = "HID backend is not running";
        return false;
    }
    if (bytes.empty()) {
        error = "feature report is empty";
        return false;
    }
    if (!hid->send_feature_report(deviceId, bytes.data(), bytes.size())) {
        error = "HID feature report write failed";
        return false;
    }
    return true;
}

bool HidToolService::write_output(std::string_view deviceId,
                                  const std::vector<uint8_t>& bytes,
                                  std::string& error) {
    prepare();
    backend::HidBackend* hid = backend();
    if (hid == nullptr) {
        error = "HID backend is not running";
        return false;
    }
    if (bytes.empty()) {
        error = "output report is empty";
        return false;
    }
    if (!hid->send_output_report(deviceId, bytes.data(), bytes.size())) {
        error = "HID output report write failed";
        return false;
    }
    return true;
}

bool HidToolService::haptics(std::string_view deviceId, float left,
                             float right, std::string& error) {
    SendHapticImpulseCommand low;
    low.motorChannel =
        static_cast<int>(SendHapticImpulseCommand::Motor::LowFrequency);
    low.amplitude = std::clamp(left, 0.0f, 1.0f);
    low.duration = 0.1f;
    DeviceCommand lowCommand{kSendHapticImpulseCommand, &low};
    if (!write_command(deviceId, lowCommand, error))
        return false;

    SendHapticImpulseCommand high;
    high.motorChannel =
        static_cast<int>(SendHapticImpulseCommand::Motor::HighFrequency);
    high.amplitude = std::clamp(right, 0.0f, 1.0f);
    high.duration = 0.1f;
    DeviceCommand highCommand{kSendHapticImpulseCommand, &high};
    return write_command(deviceId, highCommand, error);
}

bool HidToolService::lightbar(std::string_view deviceId, float red,
                              float green, float blue, std::string& error) {
    SetLightBarColorCommand command;
    command.red = std::clamp(red, 0.0f, 1.0f);
    command.green = std::clamp(green, 0.0f, 1.0f);
    command.blue = std::clamp(blue, 0.0f, 1.0f);
    DeviceCommand deviceCommand{kSetLightBarColorCommand, &command};
    return write_command(deviceId, deviceCommand, error);
}

bool HidToolService::adaptive_trigger(
    std::string_view deviceId, bool left, uint8_t mode,
    const std::vector<uint8_t>& parameters, std::string& error) {
    SetAdaptiveTriggerCommand command;
    command.trigger = left ? SetAdaptiveTriggerCommand::Trigger::Left
                           : SetAdaptiveTriggerCommand::Trigger::Right;
    command.effect.mode = mode;
    const std::size_t count = std::min<std::size_t>(
        parameters.size(), sizeof(command.effect.parameters));
    std::copy_n(parameters.begin(), count, command.effect.parameters);
    DeviceCommand deviceCommand{kSetAdaptiveTriggerCommand, &command};
    return write_command(deviceId, deviceCommand, error);
}

bool HidToolService::switch_subcommand(std::string_view deviceId,
                                       uint8_t subcommand,
                                       const std::vector<uint8_t>& payload,
                                       std::string& error) {
    SendSwitchSubcommandCommand command;
    command.subcommand = subcommand;
    command.data = payload.empty() ? nullptr : payload.data();
    command.size = static_cast<uint32_t>(payload.size());
    DeviceCommand deviceCommand{kSendSwitchSubcommandCommand, &command};
    return write_command(deviceId, deviceCommand, error);
}

std::vector<HidToolError> HidToolService::errors(std::size_t maxErrors) {
    std::vector<HidToolError> result;
    const std::size_t count =
        maxErrors == 0 ? m_errors.size() : std::min(maxErrors, m_errors.size());
    result.reserve(count);
    const std::size_t start = m_errors.size() - count;
    for (std::size_t index = start; index < m_errors.size(); ++index)
        result.push_back(m_errors[index]);
    return result;
}

void HidToolService::clear_errors() {
    m_errors.clear();
}

HidToolService::Monitor* HidToolService::find_monitor(
    std::string_view monitorId) {
    const auto it = m_monitors.find(std::string(monitorId));
    return it == m_monitors.end() ? nullptr : &it->second;
}

Device* HidToolService::find_device(std::string_view deviceId) {
    backend::HidBackend* hid = backend();
    if (hid == nullptr)
        return nullptr;
    const int id = hid->device_id(deviceId);
    if (id == 0)
        return nullptr;
    return m_preview.manager().device_by_id(id);
}

bool HidToolService::write_command(std::string_view deviceId,
                                   DeviceCommand& command,
                                   std::string& error) {
    prepare();
    Device* device = find_device(deviceId);
    if (device == nullptr) {
        error = "HID device not found: " + std::string(deviceId);
        return false;
    }
    if (m_preview.manager().send_device_command(device, &command) <= 0) {
        error = "HID device command failed";
        add_error(backend::HidErrorCode::WriteFailed, deviceId, error);
        return false;
    }
    return true;
}

void HidToolService::on_report(const backend::HidReport& report) {
    for (auto& entry : m_monitors) {
        Monitor& monitor = entry.second;
        if (monitor.paused || monitor.deviceId != report.deviceId)
            continue;
        if (std::find(monitor.types.begin(), monitor.types.end(),
                      report.kind) == monitor.types.end())
            continue;

        HidMonitorReport item;
        item.sequence = ++monitor.nextSequence;
        item.deviceId = report.deviceId;
        item.kind = report.kind;
        item.data = report.data;
        item.time = report.time;
        if (monitor.maxReports != 0 &&
            monitor.reports.size() >= monitor.maxReports) {
            monitor.reports.pop_front();
            ++monitor.dropped;
        }
        monitor.reports.push_back(std::move(item));
    }
}

void HidToolService::on_error(const backend::HidError& error) {
    add_error(error.code, error.deviceId, error.message);
}

void HidToolService::add_error(backend::HidErrorCode code,
                               std::string_view deviceId, std::string message) {
    HidToolError error;
    error.code = code;
    error.deviceId.assign(deviceId.data(), deviceId.size());
    error.message = std::move(message);
    error.time = m_preview.runtime().now();
    m_errors.push_back(std::move(error));
    while (m_errors.size() > 100)
        m_errors.pop_front();
}

} // namespace insys::editor
