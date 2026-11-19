#pragma once

#if defined(__APPLE__) && !defined(INSYS_IOS)

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace insys::backend {

class HidBackend;
class Runtime;

class MacOsHid {
public:
    MacOsHid();
    ~MacOsHid();

    MacOsHid(const MacOsHid&) = delete;
    MacOsHid& operator=(const MacOsHid&) = delete;

    bool initialize(HidBackend& backend, Runtime& runtime);
    void shutdown();
    void poll();
    std::size_t device_count() const;
    bool write_report(std::string_view id, const uint8_t* data, std::size_t size);
    bool write_feature_report(std::string_view id, const uint8_t* data,
                              std::size_t size);
    int read_feature_report(std::string_view id, uint8_t reportId, uint8_t* data,
                            std::size_t size);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace insys::backend

#endif
