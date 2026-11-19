#include "insys/backend/windows_hid.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <winioctl.h>

#include <atomic>
#include <cctype>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "insys/backend/hid_backend.h"
#include "insys/backend/hid_report_queue.h"
#include "insys/backend/runtime.h"
#include "insys/hid/hid_gamepad.h"

#ifndef IOCTL_HID_GET_REPORT_DESCRIPTOR
#define IOCTL_HID_GET_REPORT_DESCRIPTOR \
    CTL_CODE(FILE_DEVICE_KEYBOARD, 0x0001, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

namespace insys::backend {
namespace {

std::string wide_to_utf8(const wchar_t* text) {
    if (text == nullptr || *text == L'\0')
        return {};
    const int length = static_cast<int>(std::wcslen(text));
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text,
                                         length, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length,
                        result.data(), size, nullptr, nullptr);
    return result;
}

template <typename Reader>
std::string read_hid_string(HANDLE handle, Reader reader) {
    wchar_t buffer[256]{};
    if (!reader(handle, buffer, static_cast<ULONG>(sizeof(buffer))))
        return {};
    return wide_to_utf8(buffer);
}

std::vector<uint8_t> read_report_descriptor(HANDLE handle) {
    std::vector<uint8_t> descriptor(4096, 0);
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(handle, IOCTL_HID_GET_REPORT_DESCRIPTOR, nullptr, 0,
                         descriptor.data(), static_cast<DWORD>(descriptor.size()),
                         &bytesReturned, nullptr) ||
        bytesReturned == 0) {
        return {};
    }
    descriptor.resize(bytesReturned);
    return descriptor;
}

bool is_supported_usage(const HIDP_CAPS& caps) {
    if (caps.UsagePage != 0x01)
        return false;
    return caps.Usage == 0x04 || caps.Usage == 0x05 || caps.Usage == 0x08;
}

std::string device_path(const SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail) {
    if (detail == nullptr || detail->DevicePath[0] == L'\0')
        return {};
    return wide_to_utf8(detail->DevicePath);
}

std::string lower_ascii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

void detect_transport(hid::DeviceInfo& info, const std::string& enumerator,
                      const std::string& path) {
    const std::string enumeratorLower = lower_ascii(enumerator);
    if (enumeratorLower == "bthenum" || enumeratorLower == "bthle" ||
        lower_ascii(path).find("bth") != std::string::npos) {
        info.transport = hid::HidTransport::Bluetooth;
        info.bluetooth = true;
        info.bluetoothKnown = true;
        return;
    }
    if (enumeratorLower == "usb") {
        info.transport = hid::HidTransport::Usb;
        info.bluetooth = false;
        info.bluetoothKnown = true;
        return;
    }
    if (!enumeratorLower.empty()) {
        info.transport = hid::HidTransport::Other;
        info.bluetoothKnown = false;
    }
}

std::string registry_string(HDEVINFO deviceInfo, SP_DEVINFO_DATA& deviceData,
                            DWORD property) {
    wchar_t buffer[256]{};
    DWORD type = 0;
    DWORD size = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(
            deviceInfo, &deviceData, property, &type,
            reinterpret_cast<PBYTE>(buffer), sizeof(buffer), &size)) {
        return {};
    }
    if (type != REG_SZ || size < sizeof(wchar_t))
        return {};
    return wide_to_utf8(buffer);
}

struct EnumeratedDevice {
    std::string path;
    std::wstring nativePath;
    hid::DeviceInfo info;
    uint16_t inputReportSize = 0;
};

std::vector<EnumeratedDevice> enumerate_devices() {
    std::vector<EnumeratedDevice> result;
    std::unordered_set<std::string> seenPaths;

    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO deviceInfo = SetupDiGetClassDevsW(
        &hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE)
        return result;

    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(deviceInfo, nullptr, &hidGuid, index,
                                         &interfaceData)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS)
                break;
            continue;
        }

        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, nullptr, 0,
                                         &requiredSize, nullptr);
        if (requiredSize == 0)
            continue;

        std::vector<uint8_t> detailBuffer(requiredSize);
        auto* detail =
            reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA deviceData{};
        deviceData.cbSize = sizeof(deviceData);
        if (!SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, detail,
                                              requiredSize, nullptr, &deviceData))
            continue;

        std::string path = device_path(detail);
        if (path.empty() || !seenPaths.insert(path).second)
            continue;

        HANDLE handle = CreateFileW(
            detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            handle = CreateFileW(detail->DevicePath, GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        }
        if (handle == INVALID_HANDLE_VALUE)
            continue;

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (!HidD_GetPreparsedData(handle, &preparsed)) {
            CloseHandle(handle);
            continue;
        }

        HIDP_CAPS caps{};
        const NTSTATUS status = HidP_GetCaps(preparsed, &caps);
        HidD_FreePreparsedData(preparsed);
        if (status != HIDP_STATUS_SUCCESS || !is_supported_usage(caps) ||
            caps.InputReportByteLength == 0) {
            CloseHandle(handle);
            continue;
        }

        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (!HidD_GetAttributes(handle, &attributes)) {
            CloseHandle(handle);
            continue;
        }

        EnumeratedDevice enumerated;
        enumerated.path = std::move(path);
        enumerated.nativePath = detail->DevicePath;
        enumerated.inputReportSize = caps.InputReportByteLength;

        hid::DeviceInfo& info = enumerated.info;
        info.id = enumerated.path;
        info.vendorId = attributes.VendorID;
        info.productId = attributes.ProductID;
        info.usagePage = caps.UsagePage;
        info.usage = caps.Usage;
        info.inputReportSize = caps.InputReportByteLength;
        info.outputReportSize = caps.OutputReportByteLength;
        info.featureReportSize = caps.FeatureReportByteLength;
        info.reportDescriptor = read_report_descriptor(handle);
        hid::DeviceDescriptor parsedDescriptor;
        if (hid::parse_report_descriptor(info.reportDescriptor.data(),
                                         info.reportDescriptor.size(),
                                         parsedDescriptor)) {
            bool hasInputReportId = false;
            for (const hid::ReportInfo& report : parsedDescriptor.reports) {
                if (report.type == hid::ReportType::Input && report.hasId) {
                    hasInputReportId = true;
                    break;
                }
            }
            info.reportDataIncludesId = hasInputReportId;
        }

        info.description.interfaceName = "HID";
        info.description.manufacturer =
            read_hid_string(handle, HidD_GetManufacturerString);
        info.description.product =
            read_hid_string(handle, HidD_GetProductString);
        info.description.serial =
            read_hid_string(handle, HidD_GetSerialNumberString);
        info.description.version = std::to_string(attributes.VersionNumber);
        detect_transport(info,
                         registry_string(deviceInfo, deviceData,
                                         SPDRP_ENUMERATOR_NAME),
                         enumerated.path);

        CloseHandle(handle);
        result.push_back(std::move(enumerated));
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);
    return result;
}

} // namespace

struct WindowsHid::Impl {
    struct Device {
        std::string id;
        HANDLE handle = INVALID_HANDLE_VALUE;
        HANDLE stopEvent = nullptr;
        std::thread reader;
        std::atomic<bool> stop{false};
        std::atomic<bool> finished{false};
        uint16_t inputReportSize = 0;
        uint16_t featureReportSize = 0;
    };

    HidBackend* backend = nullptr;
    Runtime* runtime = nullptr;
    HidReportQueue queue;
    std::unordered_map<std::string, std::unique_ptr<Device>> devices;
    std::unordered_set<std::string> reportedOpenFailures;
    double nextRefresh = 0.0;
    bool running = false;

    void report_open_failure(const std::string& id, DWORD error) {
        if (backend == nullptr || !reportedOpenFailures.insert(id).second)
            return;
        const HidErrorCode code =
            error == ERROR_ACCESS_DENIED ? HidErrorCode::PermissionDenied
                                         : HidErrorCode::DeviceOpenFailed;
        backend->report_error(
            code, id,
            "failed to open HID device (Win32 error " +
                std::to_string(static_cast<unsigned long>(error)) + ")");
    }

    void read_loop(Device& device) {
        std::vector<uint8_t> buffer(device.inputReportSize, 0);
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr) {
            if (backend != nullptr) {
                backend->report_error(
                    HidErrorCode::ReadFailed, device.id,
                    "failed to create HID read event (Win32 error " +
                        std::to_string(static_cast<unsigned long>(
                            GetLastError())) +
                        ")");
            }
            device.finished.store(true);
            return;
        }

        while (!device.stop.load()) {
            ResetEvent(overlapped.hEvent);
            DWORD bytesRead = 0;
            BOOL ok = ReadFile(device.handle, buffer.data(),
                               static_cast<DWORD>(buffer.size()), &bytesRead,
                               &overlapped);
            if (!ok) {
                const DWORD error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    if (error != ERROR_OPERATION_ABORTED) {
                        if (backend != nullptr) {
                            backend->report_error(
                                HidErrorCode::ReadFailed, device.id,
                                "HID ReadFile failed (Win32 error " +
                                    std::to_string(
                                        static_cast<unsigned long>(error)) +
                                    ")");
                        }
                        device.finished.store(true);
                    }
                    break;
                }

                HANDLE events[2] = {overlapped.hEvent, device.stopEvent};
                const DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (wait == WAIT_OBJECT_0 + 1) {
                    CancelIoEx(device.handle, &overlapped);
                    break;
                }
                if (wait != WAIT_OBJECT_0) {
                    if (backend != nullptr) {
                        backend->report_error(
                            HidErrorCode::ReadFailed, device.id,
                            "HID read wait failed (result " +
                                std::to_string(
                                    static_cast<unsigned long>(wait)) +
                                ")");
                    }
                    device.finished.store(true);
                    break;
                }

                ok = GetOverlappedResult(device.handle, &overlapped, &bytesRead,
                                         FALSE);
                if (!ok) {
                    const DWORD overlappedError = GetLastError();
                    if (overlappedError != ERROR_OPERATION_ABORTED) {
                        if (backend != nullptr) {
                            backend->report_error(
                                HidErrorCode::ReadFailed, device.id,
                                "HID overlapped read failed (Win32 error " +
                                    std::to_string(
                                        static_cast<unsigned long>(overlappedError)) +
                                    ")");
                        }
                        device.finished.store(true);
                    }
                    break;
                }
            }

            if (bytesRead > 0) {
                queue.push_report(device.id, buffer.data(),
                                  static_cast<std::size_t>(bytesRead));
            }
        }

        CloseHandle(overlapped.hEvent);
        device.finished.store(true);
    }

    void drain_queue() {
        if (backend == nullptr)
            return;
        queue.drain(*backend);
        const std::size_t dropped = queue.take_dropped_count();
        if (dropped != 0) {
            backend->report_error(
                HidErrorCode::ReportQueueOverflow, {},
                "dropped " + std::to_string(dropped) + " HID reports");
        }
    }

    void stop_device(Device& device) {
        device.stop.store(true);
        if (device.stopEvent != nullptr)
            SetEvent(device.stopEvent);
        if (device.handle != INVALID_HANDLE_VALUE)
            CancelIoEx(device.handle, nullptr);
        if (device.reader.joinable())
            device.reader.join();
        if (device.handle != INVALID_HANDLE_VALUE) {
            CloseHandle(device.handle);
            device.handle = INVALID_HANDLE_VALUE;
        }
        if (device.stopEvent != nullptr) {
            CloseHandle(device.stopEvent);
            device.stopEvent = nullptr;
        }
    }

    bool start_device(EnumeratedDevice enumerated) {
        if (backend == nullptr || enumerated.inputReportSize == 0)
            return false;

        auto device = std::make_unique<Device>();
        device->id = enumerated.info.id;
        device->inputReportSize = enumerated.inputReportSize;
        device->featureReportSize = enumerated.info.featureReportSize;
        device->handle = CreateFileW(
            enumerated.nativePath.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED, nullptr);
        if (device->handle == INVALID_HANDLE_VALUE) {
            device->handle = CreateFileW(
                enumerated.nativePath.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED, nullptr);
        }
        if (device->handle == INVALID_HANDLE_VALUE) {
            report_open_failure(enumerated.info.id, GetLastError());
            return false;
        }
        device->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (device->stopEvent == nullptr) {
            CloseHandle(device->handle);
            return false;
        }

        if (!backend->add_device(enumerated.info)) {
            CloseHandle(device->stopEvent);
            CloseHandle(device->handle);
            return false;
        }
        reportedOpenFailures.erase(enumerated.info.id);

        Device* raw = device.get();
        devices.emplace(enumerated.info.id, std::move(device));
        raw->reader = std::thread([this, raw]() { read_loop(*raw); });
        return true;
    }

    void remove_device(std::unordered_map<std::string, std::unique_ptr<Device>>::iterator it) {
        if (it == devices.end())
            return;
        stop_device(*it->second);
        queue.discard(it->first);
        if (backend != nullptr)
            backend->remove_device(it->first);
        devices.erase(it);
    }

    void refresh() {
        auto enumerated = enumerate_devices();

        std::unordered_set<std::string> present;
        present.reserve(enumerated.size());
        for (auto& entry : enumerated) {
            present.insert(entry.path);
            if (devices.find(entry.path) == devices.end())
                start_device(std::move(entry));
        }

        for (auto it = devices.begin(); it != devices.end();) {
            if (present.find(it->first) == present.end())
                remove_device(it++);
            else
                ++it;
        }
    }

    void remove_finished() {
        for (auto it = devices.begin(); it != devices.end();) {
            Device& device = *it->second;
            if (device.finished.load()) {
                if (device.reader.joinable())
                    device.reader.join();
                queue.discard(it->first);
                if (backend != nullptr)
                    backend->remove_device(it->first);
                if (device.handle != INVALID_HANDLE_VALUE)
                    CloseHandle(device.handle);
                if (device.stopEvent != nullptr)
                    CloseHandle(device.stopEvent);
                it = devices.erase(it);
            } else {
                ++it;
            }
        }
    }
};

WindowsHid::WindowsHid() : m_impl(std::make_unique<Impl>()) {}

WindowsHid::~WindowsHid() {
    shutdown();
}

bool WindowsHid::initialize(HidBackend& backend, Runtime& runtime) {
    if (m_impl->running)
        shutdown();

    m_impl->backend = &backend;
    m_impl->runtime = &runtime;
    m_impl->running = true;
    m_impl->refresh();
    m_impl->nextRefresh = runtime.now() + 0.5;
    return true;
}

void WindowsHid::shutdown() {
    if (!m_impl->running)
        return;

    for (auto& entry : m_impl->devices) {
        m_impl->stop_device(*entry.second);
        if (m_impl->backend != nullptr)
            m_impl->backend->remove_device(entry.first);
    }
    m_impl->devices.clear();
    m_impl->queue.clear();
    m_impl->backend = nullptr;
    m_impl->runtime = nullptr;
    m_impl->running = false;
}

std::size_t WindowsHid::device_count() const {
    return m_impl->devices.size();
}

void WindowsHid::poll() {
    if (!m_impl->running)
        return;

    m_impl->drain_queue();
    m_impl->remove_finished();

    if (m_impl->runtime == nullptr)
        return;
    const double now = m_impl->runtime->now();
    if (now < m_impl->nextRefresh)
        return;
    m_impl->nextRefresh = now + 0.5;
    m_impl->refresh();
}

bool WindowsHid::write_report(std::string_view id, const uint8_t* data,
                              std::size_t size) {
    if (!m_impl->running || data == nullptr || size == 0)
        return false;

    const auto it = m_impl->devices.find(std::string(id));
    if (it == m_impl->devices.end())
        return false;

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (HidD_SetOutputReport(it->second->handle,
                                 const_cast<uint8_t*>(data),
                                 static_cast<ULONG>(size)) != FALSE)
            return true;
        if (attempt < 2)
            Sleep(1);
    }
    return false;
}

bool WindowsHid::write_feature_report(std::string_view id, const uint8_t* data,
                                      std::size_t size) {
    if (!m_impl->running || data == nullptr || size == 0)
        return false;

    const auto it = m_impl->devices.find(std::string(id));
    if (it == m_impl->devices.end())
        return false;

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (HidD_SetFeature(it->second->handle, const_cast<uint8_t*>(data),
                            static_cast<ULONG>(size)) != FALSE)
            return true;
        if (attempt < 2)
            Sleep(1);
    }
    return false;
}

int WindowsHid::read_feature_report(std::string_view id, uint8_t reportId,
                                    uint8_t* data, std::size_t size) {
    if (!m_impl->running || data == nullptr || size == 0)
        return -1;

    const auto it = m_impl->devices.find(std::string(id));
    if (it == m_impl->devices.end())
        return -1;

    const std::size_t required = it->second->featureReportSize;
    if (required == 0 || size < required)
        return -1;

    for (int attempt = 0; attempt < 3; ++attempt) {
        data[0] = reportId;
        if (HidD_GetFeature(it->second->handle, data,
                            static_cast<ULONG>(required)) != FALSE)
            return static_cast<int>(required);
        if (attempt < 2)
            Sleep(1);
    }
    return -1;
}

} // namespace insys::backend

#endif


