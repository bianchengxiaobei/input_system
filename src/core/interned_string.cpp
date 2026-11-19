#include "insys/interned_string.h"

#include <mutex>
#include <unordered_map>

namespace insys {
namespace {

struct Entry {
    uint32_t id = 0;
};

struct Registry {
    std::mutex mutex;
    std::unordered_map<std::string, Entry> entries;
    uint32_t nextId = 1;
};

Registry& registry() {
    static Registry instance;
    return instance;
}

const std::string& empty_string() {
    static const std::string empty;
    return empty;
}

} // namespace

InternedString::InternedString(const char* text)
    : InternedString(std::string_view(text ? text : "")) {}

InternedString::InternedString(std::string_view text) {
    if (text.empty())
        return;

    Registry& reg = registry();
    std::lock_guard<std::mutex> lock(reg.mutex);

    std::string key(text);
    auto it = reg.entries.find(key);
    if (it == reg.entries.end()) {
        Entry entry;
        entry.id = reg.nextId++;
        it = reg.entries.emplace(std::move(key), entry).first;
    }

    m_ptr = &it->first;
    m_id = it->second.id;
}

const std::string& InternedString::str() const {
    return m_ptr ? *m_ptr : empty_string();
}

const char* InternedString::c_str() const {
    return str().c_str();
}

} // namespace insys
