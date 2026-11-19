#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace insys::backend {

class HidBackend;

class HidReportQueue {
public:
    static constexpr std::size_t kDefaultCapacity = 4096;

    explicit HidReportQueue(std::size_t capacity = kDefaultCapacity);

    bool push_report(std::string_view id, const uint8_t* data, std::size_t size);
    bool push_removal(std::string_view id);
    std::size_t drain(HidBackend& backend);
    void discard(std::string_view id);
    void clear();
    bool empty() const;
    std::size_t pending() const;
    std::size_t capacity() const;
    void set_capacity(std::size_t capacity);
    std::size_t take_dropped_count();

private:
    struct Entry {
        std::string id;
        std::vector<uint8_t> data;
        bool removal = false;
    };

    void drop_oldest_locked();

    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;
    std::size_t m_capacity = kDefaultCapacity;
    std::size_t m_dropped = 0;
};

} // namespace insys::backend
