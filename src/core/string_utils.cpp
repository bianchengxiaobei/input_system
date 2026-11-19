#include "insys/string_utils.h"

#include <cctype>

namespace insys {

std::string to_lower(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char character : text)
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    return result;
}

bool equals_ignore_case(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

bool starts_with_ignore_case(std::string_view text, std::string_view prefix) {
    if (prefix.size() > text.size())
        return false;
    return equals_ignore_case(text.substr(0, prefix.size()), prefix);
}

} // namespace insys
