#include "insys/actions/descriptor_parser.h"

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <string>

#include "insys/string_utils.h"

namespace insys {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

std::vector<std::string_view> split_top_level(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    int depth = 0;
    bool inQuotes = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char character = text[i];
        if (character == '"' || character == '\'') {
            if (inQuotes && i > 0 && text[i - 1] == '\\')
                continue;
            inQuotes = !inQuotes;
        } else if (!inQuotes) {
            if (character == '(')
                ++depth;
            else if (character == ')')
                --depth;
            else if (character == separator && depth == 0) {
                parts.push_back(text.substr(start, i - start));
                start = i + 1;
            }
        }
    }
    parts.push_back(text.substr(start));
    return parts;
}

PrimitiveValue parse_value(std::string_view text) {
    text = trim(text);
    if (text.empty())
        return PrimitiveValue::from_bool(true);

    if (text.size() >= 2 &&
        ((text.front() == '"' && text.back() == '"') ||
         (text.front() == '\'' && text.back() == '\''))) {
        return PrimitiveValue::from_string(std::string(text.substr(1, text.size() - 2)));
    }

    if (equals_ignore_case(text, "true"))
        return PrimitiveValue::from_bool(true);
    if (equals_ignore_case(text, "false"))
        return PrimitiveValue::from_bool(false);

    std::string buffer(text);
    char* end = nullptr;
    const long long integer = std::strtoll(buffer.c_str(), &end, 10);
    if (end != nullptr && *end == '\0')
        return PrimitiveValue::from_int(static_cast<int32_t>(integer));

    end = nullptr;
    const float floating = std::strtof(buffer.c_str(), &end);
    if (end != nullptr && *end == '\0')
        return PrimitiveValue::from_float(floating);

    return PrimitiveValue::from_string(buffer);
}

NamedValue parse_parameter(std::string_view text) {
    text = trim(text);
    NamedValue parameter;

    std::size_t equals = std::string_view::npos;
    bool inQuotes = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char character = text[i];
        if (character == '"' || character == '\'')
            inQuotes = !inQuotes;
        else if (character == '=' && !inQuotes) {
            equals = i;
            break;
        }
    }

    if (equals == std::string_view::npos) {
        parameter.name = std::string(trim(text));
        parameter.value = PrimitiveValue::from_bool(true);
        return parameter;
    }

    parameter.name = std::string(trim(text.substr(0, equals)));
    parameter.value = parse_value(text.substr(equals + 1));
    return parameter;
}

} // namespace

NameAndParameters parse_item(std::string_view text) {
    NameAndParameters item;
    text = trim(text);
    if (text.empty())
        return item;

    const std::size_t open = text.find('(');
    if (open == std::string_view::npos || text.back() != ')') {
        item.name = std::string(text);
        return item;
    }

    item.name = std::string(trim(text.substr(0, open)));
    const std::string_view parameters = text.substr(open + 1, text.size() - open - 2);
    if (trim(parameters).empty())
        return item;

    for (std::string_view part : split_top_level(parameters, ',')) {
        part = trim(part);
        if (!part.empty())
            item.parameters.push_back(parse_parameter(part));
    }
    return item;
}

std::vector<NameAndParameters> parse_list(std::string_view text) {
    std::vector<NameAndParameters> result;
    text = trim(text);
    if (text.empty())
        return result;

    for (std::string_view part : split_top_level(text, ';')) {
        part = trim(part);
        if (!part.empty())
            result.push_back(parse_item(part));
    }
    return result;
}

} // namespace insys
