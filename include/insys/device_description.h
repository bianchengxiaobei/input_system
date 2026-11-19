#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace insys {

struct DeviceDescription {
    std::string interfaceName;
    std::string deviceClass;
    std::string manufacturer;
    std::string product;
    std::string serial;
    std::string version;
    std::string capabilities;
};

class DeviceMatcher {
public:
    void add(std::string_view key, std::string_view pattern, bool regex = false);
    float match_score(const DeviceDescription& description) const;
    bool empty() const { return m_rules.empty(); }

    struct Rule {
        std::string key;
        std::string pattern;
        bool regex = false;
    };

private:
    std::vector<Rule> m_rules;
};

} // namespace insys
