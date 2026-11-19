#include "insys/device_description.h"

#include <regex>

#include "insys/string_utils.h"

namespace insys {
namespace {

const std::string* field_for(const DeviceDescription& description, const std::string& key) {
    if (equals_ignore_case(key, "interface"))
        return &description.interfaceName;
    if (equals_ignore_case(key, "class"))
        return &description.deviceClass;
    if (equals_ignore_case(key, "manufacturer"))
        return &description.manufacturer;
    if (equals_ignore_case(key, "product"))
        return &description.product;
    if (equals_ignore_case(key, "serial"))
        return &description.serial;
    if (equals_ignore_case(key, "version"))
        return &description.version;
    if (equals_ignore_case(key, "capabilities"))
        return &description.capabilities;
    return nullptr;
}

bool rule_matches(const DeviceMatcher::Rule& rule, const std::string& value) {
    if (rule.regex) {
        try {
            const std::regex pattern(rule.pattern, std::regex::icase);
            return std::regex_search(value, pattern);
        } catch (const std::regex_error&) {
            return false;
        }
    }

    if (!rule.pattern.empty() && rule.pattern.back() == '*')
        return starts_with_ignore_case(value, rule.pattern.substr(0, rule.pattern.size() - 1));
    return equals_ignore_case(value, rule.pattern);
}

} // namespace

void DeviceMatcher::add(std::string_view key, std::string_view pattern, bool regex) {
    m_rules.push_back(Rule{std::string(key), std::string(pattern), regex});
}

float DeviceMatcher::match_score(const DeviceDescription& description) const {
    if (m_rules.empty())
        return 0.0f;

    int matched = 0;
    for (const Rule& rule : m_rules) {
        const std::string* value = field_for(description, rule.key);
        if (value == nullptr || value->empty())
            continue;
        if (!rule_matches(rule, *value))
            return 0.0f;
        ++matched;
    }

    return static_cast<float>(matched) / static_cast<float>(m_rules.size());
}

} // namespace insys
