#include "insys/backend/hid_backend.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <utility>

#include "insys/device_commands.h"
#include "insys/hid/hid_layout.h"
#include "insys/low/event.h"
#include "insys/low/state_format.h"
#include "insys/manager.h"

namespace insys::backend {
namespace {

std::string sanitize_layout_name(std::string_view id) {
    std::string result = "HID ";
    for (char character : id) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (std::isalnum(value) != 0 || character == '_' || character == '-')
            result.push_back(character);
        else
            result.push_back('_');
    }
    return result;
}

std::string dedup_key(const hid::DeviceInfo& info) {
    if (info.description.serial.empty())
        return {};
    return std::to_string(info.vendorId) + ":" +
           std::to_string(info.productId) + ":" +
           std::to_string(info.usagePage) + ":" +
           std::to_string(info.usage) + ":" + info.description.serial;
}

bool decode_known(hid::DeviceKind kind, const uint8_t* data, std::size_t size,
                  hid::HidGamepadState& out) {
    switch (kind) {
    case hid::DeviceKind::DualShock3:
        return hid::decode_dualshock3(data, size, out);
    case hid::DeviceKind::DualShock4:
        return hid::decode_dualshock4(data, size, out);
    case hid::DeviceKind::DualSense:
        return hid::decode_dualsense(data, size, out);
    case hid::DeviceKind::SwitchPro:
        return hid::decode_switch_pro(data, size, out);
    case hid::DeviceKind::SteamController:
        return hid::decode_steam_controller(data, size, out);
    case hid::DeviceKind::SteamDeck:
        return hid::decode_steam_deck(data, size, out);
    case hid::DeviceKind::Generic:
        return false;
    }
    return false;
}

} // namespace

HidBackend::~HidBackend() {
    shutdown();
}

bool HidBackend::initialize(Runtime& runtime, Manager& manager) {
    if (m_runtime != nullptr)
        shutdown();

    m_runtime = &runtime;
    m_manager = &manager;
    m_commandHandlerId = m_runtime->add_device_command_handler(
        [this](int deviceId, DeviceCommand* command) {
            return handle_device_command(deviceId, command);
        });
    return true;
}

void HidBackend::shutdown() {
    for (auto& entry : m_slots)
        remove_slot_devices(entry.second);
    m_slots.clear();
    m_outputHandler = {};
    m_featureHandler = {};
    m_featureReader = {};
    m_errorHandler = {};
    m_reportListener = {};
    m_recentErrors.clear();

    if (m_runtime != nullptr && m_commandHandlerId != 0)
        m_runtime->remove_device_command_handler(m_commandHandlerId);
    m_commandHandlerId = 0;
    m_runtime = nullptr;
    m_manager = nullptr;
}

void HidBackend::remove_slot_devices(Slot& slot) {
    if (m_manager == nullptr)
        return;
    if (Device* device = m_manager->device_by_id(slot.deviceId))
        m_manager->remove_device(device);
    slot.deviceId = 0;
    slot.genericReports.clear();
    slot.genericState.clear();
}

bool HidBackend::add_device(hid::DeviceInfo info) {
    if (m_runtime == nullptr || m_manager == nullptr || info.id.empty())
        return false;
    if (m_slots.find(info.id) != m_slots.end())
        return false;

    const std::string deviceDedupKey = dedup_key(info);
    if (!deviceDedupKey.empty()) {
        for (const auto& entry : m_slots) {
            if (entry.second.dedupKey == deviceDedupKey) {
                report_error(HidErrorCode::DuplicateDevice, info.id,
                             "duplicate HID device serial and usage");
                return false;
            }
        }
    }

    if (info.description.interfaceName.empty())
        info.description.interfaceName = "HID";
    if (info.description.product.empty())
        info.description.product = "HID Device";
    if (info.description.serial.empty())
        info.description.serial = info.id;
    if (info.description.deviceClass.empty())
        info.description.deviceClass =
            hid::is_gamepad_kind(hid::detect_device_kind(info)) ? "Gamepad" : "HID";

    Slot slot;
    slot.id = info.id;
    slot.dedupKey = deviceDedupKey;
    slot.info = info;
    slot.kind = hid::detect_device_kind(info);
    slot.transport = info.transport;
    slot.reportDataIncludesId = info.reportDataIncludesId;
    slot.bluetooth = info.bluetooth;
    slot.bluetoothKnown = info.bluetoothKnown;
    slot.featureReportSize = info.featureReportSize;
    if (slot.transport == hid::HidTransport::Unknown && info.bluetoothKnown) {
        slot.transport = info.bluetooth ? hid::HidTransport::Bluetooth
                                        : hid::HidTransport::Usb;
    }
    if (info.transport == hid::HidTransport::Bluetooth) {
        slot.bluetooth = true;
        slot.bluetoothKnown = true;
    } else if (info.transport == hid::HidTransport::Usb) {
        slot.bluetooth = false;
        slot.bluetoothKnown = true;
    }

    hid::DeviceDescriptor descriptor;

    if (slot.kind == hid::DeviceKind::Generic) {
        if (info.reportDescriptor.empty()) {
            report_error(HidErrorCode::InvalidReport, info.id,
                         "generic HID device has no report descriptor");
            return false;
        }
        if (!hid::parse_report_descriptor(info.reportDescriptor.data(),
                                          info.reportDescriptor.size(),
                                          descriptor)) {
            report_error(HidErrorCode::InvalidReport, info.id,
                         "failed to parse HID report descriptor");
            return false;
        }

        struct PendingReport {
            const hid::ReportInfo* report = nullptr;
            uint32_t stateOffset = 0;
        };
        std::vector<PendingReport> pending;
        uint32_t stateSize = 0;
        for (const hid::ReportInfo& report : descriptor.reports) {
            if (report.type != hid::ReportType::Input ||
                report.size_in_bytes() == 0)
                continue;
            PendingReport entry;
            entry.report = &report;
            entry.stateOffset = stateSize;
            pending.push_back(entry);
            stateSize += report.size_in_bytes();
        }
        if (pending.empty()) {
            report_error(HidErrorCode::InvalidReport, info.id,
                         "HID report descriptor contains no input reports");
            return false;
        }

        const auto control_count = [&descriptor](const PendingReport& entry) {
            return std::count_if(
                descriptor.elements.begin(), descriptor.elements.end(),
                [&](const hid::Element& element) {
                    return element.reportType == hid::ReportType::Input &&
                           element.reportId == entry.report->id &&
                           !element.is_constant() && !element.is_array();
                });
        };

        std::vector<PendingReport> layoutOrder = pending;
        std::stable_sort(layoutOrder.begin(), layoutOrder.end(),
                         [&](const PendingReport& left,
                             const PendingReport& right) {
                             const auto leftCount = control_count(left);
                             const auto rightCount = control_count(right);
                             if (leftCount != rightCount)
                                 return leftCount > rightCount;
                             return left.report->sizeInBits >
                                    right.report->sizeInBits;
                         });

        std::vector<hid::GenericReportLayout> layoutReports;
        layoutReports.reserve(layoutOrder.size());
        for (const PendingReport& entry : layoutOrder) {
            layoutReports.push_back(
                {entry.report, entry.stateOffset});
        }

        const std::string generatedLayoutName = sanitize_layout_name(info.id);
        auto layout = hid::build_generic_layout(
            descriptor, layoutReports, generatedLayoutName, stateSize);
        if (!layout)
            return false;
        m_manager->layouts().add(std::move(layout));

        Device* device =
            m_manager->create_device(generatedLayoutName, info.description);
        if (device == nullptr)
            return false;
        device->description = info.description;
        slot.deviceId = device->deviceId;
        slot.genericStateSize = stateSize;
        slot.genericState.assign(stateSize, 0);

        for (const PendingReport& entry : pending) {
            GenericReportState generic;
            generic.stateOffset = entry.stateOffset;
            generic.stateSize = entry.report->size_in_bytes();
            generic.reportId = entry.report->id;
            generic.hasReportId = entry.report->hasId;
            generic.rawState.assign(generic.stateSize, 0);
            if (generic.hasReportId && !generic.rawState.empty())
                generic.rawState[0] = generic.reportId;
            slot.genericReports.push_back(std::move(generic));
        }

        slot.outputReportSize = info.outputReportSize;
        slot.layoutName = generatedLayoutName;
        m_slots.emplace(info.id, std::move(slot));
        return true;
    }

    const char* layoutName = hid::default_layout_name(slot.kind);
    slot.layoutName = layoutName != nullptr ? layoutName : "";
    slot.stateFormat = kFormatHidGamepad;
    slot.stateSize = sizeof(hid::HidGamepadState);
    slot.outputReportSize = info.outputReportSize;
    if (slot.outputReportSize == 0) {
        switch (slot.kind) {
        case hid::DeviceKind::DualShock3: slot.outputReportSize = 49; break;
        case hid::DeviceKind::DualShock4: slot.outputReportSize = 32; break;
        case hid::DeviceKind::DualSense: slot.outputReportSize = 48; break;
        case hid::DeviceKind::SwitchPro: slot.outputReportSize = 9; break;
        case hid::DeviceKind::SteamController: slot.outputReportSize = 64; break;
        default: break;
        }
    }
    slot.info.outputReportSize = static_cast<uint16_t>(slot.outputReportSize);

    Device* device = m_manager->create_device(layoutName, info.description);
    if (device == nullptr)
        return false;

    slot.deviceId = device->deviceId;
    device->description = info.description;
    m_slots.emplace(info.id, std::move(slot));
    return true;
}
bool HidBackend::remove_device(std::string_view id) {
    const auto it = m_slots.find(std::string(id));
    if (it == m_slots.end())
        return false;

    remove_slot_devices(it->second);
    m_slots.erase(it);
    return true;
}

bool HidBackend::feed_report(std::string_view id, const uint8_t* data,
                             std::size_t size, double time) {
    Slot* slot = find_slot(id);
    if (slot == nullptr || data == nullptr || size == 0)
        return false;

    slot->reportTime = time >= 0.0 ? time : m_runtime->now();
    notify_report(slot->id, HidReportKind::Input, data, size,
                  slot->reportTime);

    if (slot->kind != hid::DeviceKind::Generic) {
        if (!slot->bluetoothKnown) {
            if (slot->kind == hid::DeviceKind::DualShock4 &&
                data[0] >= 0x11 && data[0] <= 0x19) {
                slot->bluetooth = true;
                slot->bluetoothKnown = true;
                slot->transport = hid::HidTransport::Bluetooth;
            } else if (slot->kind == hid::DeviceKind::DualSense &&
                       data[0] == 0x31) {
                slot->bluetooth = true;
                slot->bluetoothKnown = true;
                slot->transport = hid::HidTransport::Bluetooth;
            } else if (data[0] == 0x01) {
                slot->bluetooth = false;
                slot->bluetoothKnown = true;
                slot->transport = hid::HidTransport::Usb;
            }
        }
        hid::HidGamepadState next{};
        if (!decode_known(slot->kind, data, size, next))
            return false;
        if (std::memcmp(&slot->gamepad, &next, sizeof(next)) == 0)
            return false;
        slot->gamepad = next;
        slot->dirty = true;
        return true;
    }

    GenericReportState* target = nullptr;
    if (!slot->reportDataIncludesId) {
        for (GenericReportState& report : slot->genericReports) {
            const std::size_t expected =
                report.hasReportId ? report.stateSize - 1u : report.stateSize;
            if (expected == size) {
                target = &report;
                break;
            }
        }
        if (target == nullptr && !slot->genericReports.empty())
            target = &slot->genericReports.front();
    } else {
        for (GenericReportState& report : slot->genericReports) {
            if (!report.hasReportId) {
                if (!target)
                    target = &report;
                continue;
            }
            if (report.reportId == data[0]) {
                target = &report;
                break;
            }
        }
    }
    if (target == nullptr)
        return false;

    target->reportTime = slot->reportTime;
    std::vector<uint8_t> next(target->stateSize, 0);
    if (target->hasReportId && !slot->reportDataIncludesId) {
        next[0] = target->reportId;
        const std::size_t copySize =
            std::min(size, static_cast<std::size_t>(target->stateSize - 1u));
        std::memcpy(next.data() + 1, data, copySize);
    } else {
        const std::size_t copySize =
            std::min(size, static_cast<std::size_t>(target->stateSize));
        std::memcpy(next.data(), data, copySize);
    }

    if (target->rawState == next)
        return false;
    target->rawState = std::move(next);
    if (target->stateOffset + target->stateSize <= slot->genericState.size()) {
        std::memcpy(slot->genericState.data() + target->stateOffset,
                    target->rawState.data(), target->stateSize);
    }
    target->dirty = true;
    slot->dirty = true;
    return true;
}
void HidBackend::poll() {
    if (m_runtime == nullptr)
        return;
    poll(m_runtime->now());
}

void HidBackend::poll(double time) {
    if (m_runtime == nullptr || m_manager == nullptr)
        return;
    for (auto& entry : m_slots) {
        Slot& slot = entry.second;
        update_switch_handshake(slot, time);
        if (!slot.dirty)
            continue;
        slot.dirty = false;
        if (slot.kind == hid::DeviceKind::Generic) {
            for (GenericReportState& report : slot.genericReports)
                report.dirty = false;
            queue_generic_state(
                slot, slot.reportTime >= 0.0 ? slot.reportTime : time);
            continue;
        }
        queue_state(slot, slot.reportTime >= 0.0 ? slot.reportTime : time);
    }
}

bool HidBackend::connected(std::string_view id) const {
    return find_slot(id) != nullptr;
}

int HidBackend::device_id(std::string_view id) const {
    const Slot* slot = find_slot(id);
    return slot == nullptr ? 0 : slot->deviceId;
}

hid::DeviceKind HidBackend::kind(std::string_view id) const {
    const Slot* slot = find_slot(id);
    return slot == nullptr ? hid::DeviceKind::Generic : slot->kind;
}

hid::HidTransport HidBackend::transport(std::string_view id) const {
    const Slot* slot = find_slot(id);
    return slot == nullptr ? hid::HidTransport::Unknown : slot->transport;
}

std::vector<HidError> HidBackend::recent_errors() const {
    return m_recentErrors;
}

void HidBackend::clear_recent_errors() {
    m_recentErrors.clear();
}

std::vector<DeviceView> HidBackend::devices() const {
    std::vector<DeviceView> result;
    result.reserve(m_slots.size());
    for (const auto& entry : m_slots) {
        DeviceView view;
        view.id = entry.second.id;
        view.info = entry.second.info;
        view.kind = entry.second.kind;
        view.layoutName = entry.second.layoutName;
        result.push_back(std::move(view));
    }
    std::sort(result.begin(), result.end(),
              [](const DeviceView& left, const DeviceView& right) {
                  return left.id < right.id;
              });
    return result;
}

void HidBackend::set_output_report_handler(OutputReportHandler handler) {
    m_outputHandler = std::move(handler);
}

void HidBackend::set_feature_report_handler(FeatureReportHandler handler) {
    m_featureHandler = std::move(handler);
}

void HidBackend::set_feature_report_reader(FeatureReportReader reader) {
    m_featureReader = std::move(reader);
}

void HidBackend::set_error_handler(ErrorHandler handler) {
    m_errorHandler = std::move(handler);
}

void HidBackend::set_report_listener(ReportListener listener) {
    m_reportListener = std::move(listener);
}

void HidBackend::notify_report(std::string_view id, HidReportKind kind,
                               const uint8_t* data, std::size_t size,
                               double time) {
    if (!m_reportListener || data == nullptr || size == 0)
        return;
    HidReport report;
    report.deviceId.assign(id.data(), id.size());
    report.kind = kind;
    report.data.assign(data, data + size);
    report.time = time;
    m_reportListener(report);
}

void HidBackend::report_error(HidErrorCode code, std::string_view id,
                              std::string message) {
    if (!m_errorHandler)
        return;
    HidError error;
    error.code = code;
    error.deviceId.assign(id.data(), id.size());
    error.message = std::move(message);
    m_recentErrors.push_back(error);
    while (m_recentErrors.size() > 100)
        m_recentErrors.erase(m_recentErrors.begin());
    m_errorHandler(error);
}

bool HidBackend::send_output_report(std::string_view id, const uint8_t* data,
                                    std::size_t size) {
    if (!m_outputHandler || data == nullptr || size == 0) {
        report_error(HidErrorCode::WriteFailed, id,
                     "no HID output report handler or empty report");
        return false;
    }

    if (m_outputHandler(id, data, size)) {
        notify_report(id, HidReportKind::Output, data, size,
                      m_runtime != nullptr ? m_runtime->now() : 0.0);
        return true;
    }

    report_error(HidErrorCode::WriteFailed, id, "HID output report failed");
    return false;
}

bool HidBackend::send_feature_report(std::string_view id, const uint8_t* data,
                                     std::size_t size) {
    if (!m_featureHandler || data == nullptr || size == 0) {
        report_error(HidErrorCode::WriteFailed, id,
                     "no HID feature report handler or empty report");
        return false;
    }

    if (m_featureHandler(id, data, size)) {
        notify_report(id, HidReportKind::Feature, data, size,
                      m_runtime != nullptr ? m_runtime->now() : 0.0);
        return true;
    }

    report_error(HidErrorCode::WriteFailed, id, "HID feature report failed");
    return false;
}

int HidBackend::read_feature_report(std::string_view id, uint8_t reportId,
                                    uint8_t* data, std::size_t size) {
    if (!m_featureReader || data == nullptr || size == 0) {
        report_error(HidErrorCode::FeatureReadFailed, id,
                     "no HID feature report reader or empty buffer");
        return -1;
    }

    const int result = m_featureReader(id, reportId, data, size);
    if (result >= 0) {
        notify_report(id, HidReportKind::Feature, data,
                      static_cast<std::size_t>(result),
                      m_runtime != nullptr ? m_runtime->now() : 0.0);
        return result;
    }

    report_error(HidErrorCode::FeatureReadFailed, id,
                 "HID feature report read failed");
    return -1;
}

void HidBackend::queue_state(Slot& slot, double time) {
    if (slot.kind == hid::DeviceKind::Generic || slot.stateSize == 0)
        return;

    const auto buffer = low::make_state_event(
        slot.stateFormat, &slot.gamepad, slot.stateSize, slot.deviceId, time);
    m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
}

void HidBackend::queue_generic_state(Slot& slot, double time) {
    if (slot.genericStateSize == 0 || slot.genericState.empty())
        return;
    const auto buffer = low::make_state_event(
        kFormatHid, slot.genericState.data(), slot.genericStateSize,
        slot.deviceId, time);
    m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
}

HidBackend::Slot* HidBackend::find_slot(std::string_view id) {
    const auto it = m_slots.find(std::string(id));
    return it == m_slots.end() ? nullptr : &it->second;
}

const HidBackend::Slot* HidBackend::find_slot(std::string_view id) const {
    const auto it = m_slots.find(std::string(id));
    return it == m_slots.end() ? nullptr : &it->second;
}

HidBackend::Slot* HidBackend::find_slot_by_device(int deviceId) {
    for (auto& entry : m_slots) {
        if (entry.second.deviceId == deviceId)
            return &entry.second;
    }
    return nullptr;
}

long HidBackend::handle_device_command(int deviceId, DeviceCommand* command) {
    if (command == nullptr || command->data == nullptr)
        return -1;

    Slot* slot = find_slot_by_device(deviceId);
    if (slot == nullptr)
        return -1;

    if (command->type == kGetHapticCapabilitiesCommand) {
        auto* request = static_cast<GetHapticCapabilitiesCommand*>(command->data);
        if (request->capabilities == nullptr)
            return -1;

        HapticCapabilities capabilities;
        capabilities.supportsLowFrequency =
            slot->kind == hid::DeviceKind::DualShock3 ||
            slot->kind == hid::DeviceKind::DualShock4 ||
            slot->kind == hid::DeviceKind::DualSense ||
            slot->kind == hid::DeviceKind::SwitchPro ||
            slot->kind == hid::DeviceKind::SteamController ||
            slot->kind == hid::DeviceKind::SteamDeck;
        capabilities.supportsHighFrequency = capabilities.supportsLowFrequency;
        capabilities.supportsLightBar =
            slot->kind == hid::DeviceKind::DualShock4 ||
            slot->kind == hid::DeviceKind::DualSense;
        capabilities.supportsLeftTrigger =
            slot->kind == hid::DeviceKind::DualSense;
        capabilities.supportsRightTrigger =
            slot->kind == hid::DeviceKind::DualSense;
        capabilities.maxBufferSize =
            slot->kind == hid::DeviceKind::Generic ? slot->outputReportSize : 0;
        *request->capabilities = capabilities;
        return 1;
    }

    if (command->type == kSendHidOutputReportCommand ||
        command->type == kSendHidFeatureReportCommand) {
        auto* request = static_cast<SendHidReportCommand*>(command->data);
        if (request->data == nullptr && request->size != 0)
            return -1;

        std::vector<uint8_t> report;
        report.reserve(request->size + (request->prependReportId ? 1u : 0u));
        if (request->prependReportId)
            report.push_back(request->reportId);
        if (request->size != 0) {
            report.insert(report.end(), request->data,
                          request->data + request->size);
        }
        if (report.empty())
            return -1;

        const bool feature =
            command->type == kSendHidFeatureReportCommand;
        const bool sent =
            feature ? send_feature_report(slot->id, report.data(), report.size())
                    : send_output_report(slot->id, report.data(), report.size());
        return sent ? 1 : -1;
    }

    if (command->type == kGetHidFeatureReportCommand) {
        auto* request = static_cast<GetHidFeatureReportCommand*>(command->data);
        if (request->data == nullptr || request->capacity == 0)
            return -1;
        const int bytes = read_feature_report(slot->id, request->reportId,
                                              request->data,
                                              request->capacity);
        if (bytes < 0)
            return -1;
        if (request->reportSize != nullptr)
            *request->reportSize = static_cast<uint32_t>(bytes);
        return bytes;
    }

    if (command->type == kSetLightBarColorCommand) {
        auto* request = static_cast<SetLightBarColorCommand*>(command->data);
        if (slot->kind != hid::DeviceKind::DualShock4 &&
            slot->kind != hid::DeviceKind::DualSense)
            return -1;
        slot->lightBar[0] = std::clamp(request->red, 0.0f, 1.0f);
        slot->lightBar[1] = std::clamp(request->green, 0.0f, 1.0f);
        slot->lightBar[2] = std::clamp(request->blue, 0.0f, 1.0f);
        slot->hasLightBar = true;
        return emit_output(*slot, slot->lightBar) ? 1 : -1;
    }

    if (command->type == kSetAdaptiveTriggerCommand) {
        auto* request = static_cast<SetAdaptiveTriggerCommand*>(command->data);
        if (slot->kind != hid::DeviceKind::DualSense)
            return -1;
        uint8_t* effect = request->trigger ==
                                  SetAdaptiveTriggerCommand::Trigger::Left
                              ? slot->leftTriggerEffect
                              : slot->rightTriggerEffect;
        effect[0] = request->effect.mode;
        std::memcpy(effect + 1, request->effect.parameters, 10);
        if (request->trigger == SetAdaptiveTriggerCommand::Trigger::Left)
            slot->hasLeftTriggerEffect = true;
        else
            slot->hasRightTriggerEffect = true;
        return emit_output(*slot, slot->hasLightBar ? slot->lightBar : nullptr)
                   ? 1
                   : -1;
    }

    if (command->type == kSendSwitchSubcommandCommand) {
        auto* request = static_cast<SendSwitchSubcommandCommand*>(command->data);
        if (slot->kind != hid::DeviceKind::SwitchPro)
            return -1;
        std::vector<uint8_t> report;
        const uint8_t packetCounter = slot->switchPacketCounter;
        hid::encode_switch_subcommand(
            static_cast<hid::SwitchSubcommand>(request->subcommand),
            request->data, request->size, report, true, packetCounter);
        bool sent = send_output_report(slot->id, report.data(), report.size());
        if (!sent) {
            hid::encode_switch_subcommand(
                static_cast<hid::SwitchSubcommand>(request->subcommand),
                request->data, request->size, report, false, packetCounter);
            sent = send_output_report(slot->id, report.data(), report.size());
        }
        if (sent)
            ++slot->switchPacketCounter;
        return sent ? 1 : -1;
    }

    if (command->type == kSendHapticImpulseCommand) {
        auto* request = static_cast<SendHapticImpulseCommand*>(command->data);
        return send_haptic_impulse(*slot, *request) ? 1 : -1;
    }

    if (command->type == kSendBufferedHapticsCommand) {
        auto* request = static_cast<SendBufferedHapticsCommand*>(command->data);
        if (request->data == nullptr || request->size == 0)
            return -1;
        return send_output_report(slot->id, request->data, request->size) ? 1 : -1;
    }

    return -1;
}

bool HidBackend::send_haptic_impulse(Slot& slot,
                                     const SendHapticImpulseCommand& request) {
    const float amplitude = std::clamp(request.amplitude, 0.0f, 1.0f);
    const bool low =
        request.motorChannel ==
        static_cast<int>(SendHapticImpulseCommand::Motor::LowFrequency);
    const bool high =
        request.motorChannel ==
        static_cast<int>(SendHapticImpulseCommand::Motor::HighFrequency);
    if (!low && !high)
        return false;

    if (low)
        slot.lowFrequency = amplitude;
    if (high)
        slot.highFrequency = amplitude;

    return emit_output(slot, slot.hasLightBar ? slot.lightBar : nullptr);
}

void HidBackend::update_switch_handshake(Slot& slot, double time) {
    if (slot.kind != hid::DeviceKind::SwitchPro || slot.switchHandshakeStep >= 8)
        return;
    if (time + 1e-9 < slot.switchHandshakeTime)
        return;

    static constexpr hid::SwitchMagicCommand sequence[] = {
        hid::SwitchMagicCommand::Status,
        hid::SwitchMagicCommand::Handshake,
        hid::SwitchMagicCommand::Highspeed,
        hid::SwitchMagicCommand::Handshake,
        hid::SwitchMagicCommand::ForceUSB,
    };

    std::vector<uint8_t> report;
    bool sent = false;
    if (slot.switchHandshakeStep < 5) {
        hid::encode_switch_magic_output(sequence[slot.switchHandshakeStep],
                                        report, true);
        sent = send_output_report(slot.id, report.data(), report.size());
        if (!sent) {
            hid::encode_switch_magic_output(sequence[slot.switchHandshakeStep],
                                            report, false);
            sent = send_output_report(slot.id, report.data(), report.size());
        }
    } else {
        uint8_t payload[1] = {0};
        hid::SwitchSubcommand subcommand = hid::SwitchSubcommand::EnableVibration;
        if (slot.switchHandshakeStep == 5) {
            subcommand = hid::SwitchSubcommand::SetInputReportMode;
            payload[0] = static_cast<uint8_t>(hid::SwitchInputReportMode::Full);
        } else if (slot.switchHandshakeStep == 6) {
            subcommand = hid::SwitchSubcommand::EnableImu;
            payload[0] = 0x01;
        } else {
            payload[0] = 0x01;
        }
        const uint8_t packetCounter = slot.switchPacketCounter;
        hid::encode_switch_subcommand(subcommand, payload, sizeof(payload),
                                      report, true, packetCounter);
        sent = send_output_report(slot.id, report.data(), report.size());
        if (!sent) {
            hid::encode_switch_subcommand(subcommand, payload, sizeof(payload),
                                          report, false, packetCounter);
            sent = send_output_report(slot.id, report.data(), report.size());
        }
        if (sent)
            ++slot.switchPacketCounter;
    }
    if (!sent)
        return;

    ++slot.switchHandshakeStep;
    slot.switchHandshakeTime = time + 0.1;
}

bool HidBackend::emit_output(Slot& slot, const float* lightBarColor) {
    std::vector<uint8_t> report;
    switch (slot.kind) {
    case hid::DeviceKind::DualShock3:
        hid::encode_dualshock3_haptic(slot.lowFrequency, slot.highFrequency,
                                      report, slot.outputReportSize);
        break;
    case hid::DeviceKind::DualShock4:
        hid::encode_dualshock4_haptic(slot.lowFrequency, slot.highFrequency,
                                      report, slot.outputReportSize,
                                      lightBarColor, slot.bluetooth,
                                      slot.outputSequence++);
        break;
    case hid::DeviceKind::DualSense:
        hid::encode_dualsense_haptic(
            slot.lowFrequency, slot.highFrequency, report,
            slot.outputReportSize, lightBarColor, slot.bluetooth,
            slot.outputSequence++,
            slot.hasLeftTriggerEffect ? slot.leftTriggerEffect : nullptr,
            slot.hasRightTriggerEffect ? slot.rightTriggerEffect : nullptr);
        break;
    case hid::DeviceKind::SwitchPro:
        hid::encode_switch_haptic(slot.lowFrequency, slot.highFrequency, report);
        break;
    case hid::DeviceKind::SteamController:
        hid::encode_steam_haptic(slot.lowFrequency, slot.highFrequency, report);
        break;
    case hid::DeviceKind::SteamDeck:
        hid::encode_steam_deck_haptic(slot.lowFrequency, slot.highFrequency,
                                      report);
        return send_feature_report(slot.id, report.data(), report.size());
    case hid::DeviceKind::Generic:
        return false;
    }

    return send_output_report(slot.id, report.data(), report.size());
}

} // namespace insys::backend

































