#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace insys {

class InternedString {
public:
    InternedString() = default;
    InternedString(const char* text);
    InternedString(std::string_view text);

    const std::string& str() const;
    const char* c_str() const;
    uint32_t id() const { return m_id; }
    bool empty() const { return m_id == 0; }

    bool operator==(const InternedString& other) const { return m_id == other.m_id; }
    bool operator!=(const InternedString& other) const { return m_id != other.m_id; }

private:
    const std::string* m_ptr = nullptr;
    uint32_t m_id = 0;
};

} // namespace insys
