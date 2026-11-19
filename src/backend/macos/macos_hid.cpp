#include "insys/backend/macos_hid.h"

#if defined(__APPLE__) && !defined(INSYS_IOS)

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDLib.h>
#include <TargetConditionals.h>

#if TARGET_OS_OSX

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
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

namespace insys::backend {
namespace {

std::string cf_string(CFStringRef string) {
    if (string == nullptr)
        return {};
    char buffer[512]{};
    if (!CFStringGetCString(string, buffer, sizeof(buffer),
                            kCFStringEncodingUTF8)) {
        return {};
    }
    return buffer;
}

std::string string_property(IOHIDDeviceRef device, CFStringRef key) {
    CFTypeRef value = IOHIDDeviceGetProperty(device, key);
    if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID())
        return {};
    return cf_string(static_cast<CFStringRef>(value));
}

int32_t int_property(IOHIDDeviceRef device, CFStringRef key) {
    CFTypeRef value = IOHIDDeviceGetProperty(device, key);
    if (value == nullptr || CFGetTypeID(value) != CFNumberGetTypeID())
        return 0;
    int32_t result = 0;
    CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt32Type,
                     &result);
    return result;
}

std::vector<uint8_t> descriptor_property(IOHIDDeviceRef device) {
    CFTypeRef value =
        IOHIDDeviceGetProperty(device, CFSTR(kIOHIDReportDescriptorKey));
    if (value == nullptr || CFGetTypeID(value) != CFDataGetTypeID())
        return {};
    const auto* data = CFDataGetBytePtr(static_cast<CFDataRef>(value));
    const CFIndex size = CFDataGetLength(static_cast<CFDataRef>(value));
    if (data == nullptr || size <= 0)
        return {};
    return std::vector<uint8_t>(data, data + size);
}

bool descriptor_has_report_id(const std::vector<uint8_t>& descriptor) {
    hid::DeviceDescriptor parsed;
    if (!hid::parse_report_descriptor(descriptor.data(), descriptor.size(),
                                      parsed))
        return false;
    for (const hid::ReportInfo& report : parsed.reports) {
        if (report.type == hid::ReportType::Input && report.hasId)
            return true;
    }
    return false;
}

hid::HidTransport transport_property(IOHIDDeviceRef device, bool& known) {
    const std::string transport =
        string_property(device, CFSTR(kIOHIDTransportKey));
    if (transport.empty()) {
        known = false;
        return hid::HidTransport::Unknown;
    }
    if (transport.rfind("Bluetooth", 0) == 0) {
        known = true;
        return hid::HidTransport::Bluetooth;
    }
    if (transport == "USB") {
        known = true;
        return hid::HidTransport::Usb;
    }
    if (transport == "I2C" || transport == "SPI") {
        known = true;
        return hid::HidTransport::Other;
    }
    known = true;
    return hid::HidTransport::Other;
}

std::string device_identifier(IOHIDDeviceRef device) {
    const io_service_t service = IOHIDDeviceGetService(device);
    if (service != MACH_PORT_NULL) {
        uint64_t entryId = 0;
        if (IORegistryEntryGetRegistryEntryID(service, &entryId) ==
            KERN_SUCCESS) {
            return "mac:" + std::to_string(entryId);
        }
    }

    std::string fallback = "mac:";
    fallback += std::to_string(int_property(device, CFSTR(kIOHIDVendorIDKey)));
    fallback += ":";
    fallback += std::to_string(int_property(device, CFSTR(kIOHIDProductIDKey)));
    const std::string serial =
        string_property(device, CFSTR(kIOHIDSerialNumberKey));
    if (!serial.empty()) {
        fallback += ":";
        fallback += serial;
    }
    return fallback;
}

hid::DeviceInfo make_device_info(IOHIDDeviceRef device) {
    hid::DeviceInfo info;
    info.id = device_identifier(device);
    info.vendorId =
        static_cast<uint32_t>(int_property(device, CFSTR(kIOHIDVendorIDKey)));
    info.productId =
        static_cast<uint32_t>(int_property(device, CFSTR(kIOHIDProductIDKey)));
    info.usagePage = static_cast<uint16_t>(
        int_property(device, CFSTR(kIOHIDPrimaryUsagePageKey)));
    info.usage = static_cast<uint16_t>(
        int_property(device, CFSTR(kIOHIDPrimaryUsageKey)));
    info.reportDescriptor = descriptor_property(device);
    info.inputReportSize = static_cast<uint16_t>(std::max(
        0, int_property(device, CFSTR(kIOHIDMaxInputReportSizeKey))));
    info.outputReportSize = static_cast<uint16_t>(std::max(
        0, int_property(device, CFSTR(kIOHIDMaxOutputReportSizeKey))));
    info.featureReportSize = static_cast<uint16_t>(std::max(
        0, int_property(device, CFSTR(kIOHIDMaxFeatureReportSizeKey))));
    info.reportDataIncludesId =
        descriptor_has_report_id(info.reportDescriptor);
    info.transport = transport_property(device, info.bluetoothKnown);
    info.bluetooth = info.transport == hid::HidTransport::Bluetooth;
    info.description.interfaceName = "HID";
    info.description.manufacturer =
        string_property(device, CFSTR(kIOHIDManufacturerKey));
    info.description.product =
        string_property(device, CFSTR(kIOHIDProductKey));
    info.description.serial =
        string_property(device, CFSTR(kIOHIDSerialNumberKey));
    const int32_t version =
        int_property(device, CFSTR(kIOHIDVersionNumberKey));
    info.description.version = std::to_string(version);
    return info;
}

CFMutableDictionaryRef matching_dictionary(uint32_t usage) {
    CFMutableDictionaryRef dictionary = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (dictionary == nullptr)
        return nullptr;

    int32_t usagePage = 0x01;
    int32_t usageValue = static_cast<int32_t>(usage);
    CFNumberRef page = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type,
                                      &usagePage);
    CFNumberRef value = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type,
                                       &usageValue);
    if (page != nullptr) {
        CFDictionarySetValue(dictionary, CFSTR(kIOHIDDeviceUsagePageKey), page);
        CFRelease(page);
    }
    if (value != nullptr) {
        CFDictionarySetValue(dictionary, CFSTR(kIOHIDDeviceUsageKey), value);
        CFRelease(value);
    }
    return dictionary;
}

} // namespace

struct MacOsHid::Impl {
    struct Device {
        Impl* owner = nullptr;
        std::string id;
        IOHIDDeviceRef handle = nullptr;
        std::vector<uint8_t> inputBuffer;
        uint16_t featureReportSize = 0;
    };

    HidBackend* backend = nullptr;
    Runtime* runtime = nullptr;
    IOHIDManagerRef manager = nullptr;
    HidReportQueue queue;
    std::unordered_map<std::string, std::unique_ptr<Device>> devices;
    std::mutex mutex;
    double nextRefresh = 0.0;
    bool running = false;

    static void input_report_callback(void* context, IOReturn result,
                                      void* sender, IOHIDReportType type,
                                      uint32_t reportId, uint8_t* report,
                                      CFIndex reportLength) {
        (void)result;
        (void)sender;
        (void)type;
        auto* device = static_cast<Device*>(context);
        if (device == nullptr || device->owner == nullptr ||
            !device->owner->running || report == nullptr ||
            reportLength <= 0) {
            return;
        }

        if (reportId != 0 && report[0] != reportId) {
            std::vector<uint8_t> prefixed(
                static_cast<std::size_t>(reportLength) + 1);
            prefixed[0] = static_cast<uint8_t>(reportId);
            std::memcpy(prefixed.data() + 1, report,
                        static_cast<std::size_t>(reportLength));
            device->owner->queue.push_report(device->id, prefixed.data(),
                                             prefixed.size());
            return;
        }
        device->owner->queue.push_report(
            device->id, report, static_cast<std::size_t>(reportLength));
    }

    void start_device(IOHIDDeviceRef handle) {
        if (backend == nullptr)
            return;

        hid::DeviceInfo info = make_device_info(handle);
        if (info.id.empty() || info.inputReportSize == 0)
            return;
        if (!backend->add_device(info))
            return;

        auto device = std::make_unique<Device>();
        device->owner = this;
        device->id = info.id;
        device->handle = handle;
        device->inputBuffer.assign(info.inputReportSize, 0);
        device->featureReportSize = info.featureReportSize;
        CFRetain(handle);

        IOHIDDeviceRegisterInputReportCallback(
            handle, device->inputBuffer.data(),
            static_cast<CFIndex>(device->inputBuffer.size()),
            &Impl::input_report_callback, device.get());
        IOHIDDeviceScheduleWithRunLoop(handle, CFRunLoopGetMain(),
                                       kCFRunLoopDefaultMode);

        devices.emplace(device->id, std::move(device));
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

    void stop_device(
        std::unordered_map<std::string, std::unique_ptr<Device>>::iterator it) {
        if (it == devices.end())
            return;
        Device& device = *it->second;
        IOHIDDeviceRegisterInputReportCallback(
            device.handle, device.inputBuffer.data(),
            static_cast<CFIndex>(device.inputBuffer.size()), nullptr, nullptr);
        IOHIDDeviceUnscheduleFromRunLoop(device.handle, CFRunLoopGetMain(),
                                         kCFRunLoopDefaultMode);
        IOHIDDeviceClose(device.handle, kIOHIDOptionsTypeNone);
        queue.discard(device.id);
        if (backend != nullptr)
            backend->remove_device(device.id);
        CFRelease(device.handle);
        devices.erase(it);
    }

    void refresh() {
        if (manager == nullptr)
            return;

        CFSetRef set = IOHIDManagerCopyDevices(manager);
        std::unordered_set<std::string> present;
        if (set != nullptr) {
            const CFIndex count = CFSetGetCount(set);
            std::vector<const void*> values(static_cast<std::size_t>(count));
            if (count > 0)
                CFSetGetValues(set, values.data());
            for (const void* value : values) {
                auto* handle = static_cast<IOHIDDeviceRef>(
                    const_cast<void*>(value));
                const std::string id = device_identifier(handle);
                present.insert(id);
                if (devices.find(id) == devices.end())
                    start_device(handle);
            }
            CFRelease(set);
        }

        for (auto it = devices.begin(); it != devices.end();) {
            if (present.find(it->first) == present.end())
                stop_device(it++);
            else
                ++it;
        }
    }
};

MacOsHid::MacOsHid() : m_impl(std::make_unique<Impl>()) {}

MacOsHid::~MacOsHid() {
    shutdown();
}

bool MacOsHid::initialize(HidBackend& backend, Runtime& runtime) {
    if (m_impl->running)
        shutdown();

    m_impl->backend = &backend;
    m_impl->runtime = &runtime;

    m_impl->manager = IOHIDManagerCreate(kCFAllocatorDefault,
                                         kIOHIDOptionsTypeNone);
    if (m_impl->manager == nullptr) {
        m_impl->backend = nullptr;
        m_impl->runtime = nullptr;
        return false;
    }

    CFMutableArrayRef matching = CFArrayCreateMutable(
        kCFAllocatorDefault, 3, &kCFTypeArrayCallBacks);
    if (matching != nullptr) {
        for (uint32_t usage : {0x04u, 0x05u, 0x08u}) {
            CFMutableDictionaryRef dictionary = matching_dictionary(usage);
            if (dictionary != nullptr) {
                CFArrayAppendValue(matching, dictionary);
                CFRelease(dictionary);
            }
        }
        IOHIDManagerSetDeviceMatchingMultiple(m_impl->manager, matching);
        CFRelease(matching);
    }

    IOHIDManagerScheduleWithRunLoop(m_impl->manager, CFRunLoopGetMain(),
                                    kCFRunLoopDefaultMode);
    if (IOHIDManagerOpen(m_impl->manager, kIOHIDOptionsTypeNone) !=
        kIOReturnSuccess) {
        IOHIDManagerUnscheduleFromRunLoop(m_impl->manager, CFRunLoopGetMain(),
                                          kCFRunLoopDefaultMode);
        CFRelease(m_impl->manager);
        m_impl->manager = nullptr;
        m_impl->backend = nullptr;
        m_impl->runtime = nullptr;
        return false;
    }

    m_impl->running = true;
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
    m_impl->refresh();
    m_impl->nextRefresh = runtime.now() + 0.5;
    return true;
}

void MacOsHid::shutdown() {
    if (!m_impl->running)
        return;

    while (!m_impl->devices.empty())
        m_impl->stop_device(m_impl->devices.begin());

    if (m_impl->manager != nullptr) {
        IOHIDManagerUnscheduleFromRunLoop(m_impl->manager,
                                          CFRunLoopGetMain(),
                                          kCFRunLoopDefaultMode);
        IOHIDManagerClose(m_impl->manager, kIOHIDOptionsTypeNone);
        CFRelease(m_impl->manager);
        m_impl->manager = nullptr;
    }

    m_impl->queue.clear();
    m_impl->backend = nullptr;
    m_impl->runtime = nullptr;
    m_impl->running = false;
}

std::size_t MacOsHid::device_count() const {
    return m_impl->devices.size();
}

void MacOsHid::poll() {
    if (!m_impl->running)
        return;

    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.0, true);
    m_impl->drain_queue();

    if (m_impl->runtime == nullptr)
        return;
    const double now = m_impl->runtime->now();
    if (now < m_impl->nextRefresh)
        return;
    m_impl->nextRefresh = now + 0.5;
    m_impl->refresh();
}

bool MacOsHid::write_report(std::string_view id, const uint8_t* data,
                            std::size_t size) {
    if (!m_impl->running || data == nullptr || size < 2)
        return false;

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const auto it = m_impl->devices.find(std::string(id));
    if (it == m_impl->devices.end())
        return false;

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (IOHIDDeviceSetReport(it->second->handle, kIOHIDReportTypeOutput,
                                 data[0], data + 1, size - 1) ==
            kIOReturnSuccess)
            return true;
        if (attempt < 2)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool MacOsHid::write_feature_report(std::string_view id, const uint8_t* data,
                                    std::size_t size) {
    if (!m_impl->running || data == nullptr || size < 2)
        return false;

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const auto it = m_impl->devices.find(std::string(id));
    if (it == m_impl->devices.end())
        return false;

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (IOHIDDeviceSetReport(it->second->handle, kIOHIDReportTypeFeature,
                                 data[0], data + 1, size - 1) ==
            kIOReturnSuccess)
            return true;
        if (attempt < 2)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

int MacOsHid::read_feature_report(std::string_view id, uint8_t reportId,
                                  uint8_t* data, std::size_t size) {
    if (!m_impl->running || data == nullptr || size == 0)
        return -1;

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const auto it = m_impl->devices.find(std::string(id));
    if (it == m_impl->devices.end())
        return -1;

    const std::size_t required = it->second->featureReportSize;
    if (required == 0 || size < required)
        return -1;

    const bool hasReportId = reportId != 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (hasReportId)
            data[0] = reportId;
        CFIndex length = static_cast<CFIndex>(
            hasReportId ? required - 1u : required);
        uint8_t* buffer = hasReportId ? data + 1 : data;
        if (IOHIDDeviceGetReport(it->second->handle, kIOHIDReportTypeFeature,
                                 reportId, buffer, &length) ==
            kIOReturnSuccess) {
            return static_cast<int>(length + (hasReportId ? 1 : 0));
        }
        if (attempt < 2)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return -1;
}

} // namespace insys::backend

#endif
#endif
