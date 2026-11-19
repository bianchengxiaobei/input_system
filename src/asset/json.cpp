#include "insys/asset/json.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace insys::asset {
namespace {

void append_utf8(std::string& out, uint32_t codePoint) {
    if (codePoint <= 0x7F) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

class Parser {
public:
    Parser(std::string_view text, JsonParseError& error) : m_text(text), m_error(error) {}

    bool parse(JsonValue& out) {
        skip_whitespace();
        if (!parse_value(out))
            return false;
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected trailing content");
            return false;
        }
        return true;
    }

private:
    bool at_end() const { return m_pos >= m_text.size(); }
    char peek() const { return at_end() ? '\0' : m_text[m_pos]; }

    void advance() {
        if (at_end())
            return;
        if (m_text[m_pos] == '\n') {
            ++m_line;
            m_column = 1;
        } else {
            ++m_column;
        }
        ++m_pos;
    }

    void skip_whitespace() {
        while (!at_end()) {
            const char character = peek();
            if (character != ' ' && character != '\t' && character != '\n' &&
                character != '\r')
                break;
            advance();
        }
    }

    bool fail(const std::string& message) {
        m_error.line = m_line;
        m_error.column = m_column;
        m_error.message = message;
        return false;
    }

    bool parse_value(JsonValue& out) {
        if (at_end())
            return fail("unexpected end of input");

        const char character = peek();
        if (character == '{')
            return parse_object(out);
        if (character == '[')
            return parse_array(out);
        if (character == '"')
            return parse_string(out);
        if (character == 't' || character == 'f')
            return parse_bool(out);
        if (character == 'n')
            return parse_null(out);
        if (character == '-' || (character >= '0' && character <= '9'))
            return parse_number(out);
        return fail("unexpected character");
    }

    bool parse_object(JsonValue& out) {
        out = JsonValue{};
        out.type = JsonValue::Type::Object;
        out.line = m_line;
        out.column = m_column;
        advance();
        skip_whitespace();
        if (peek() == '}') {
            advance();
            return true;
        }

        while (true) {
            skip_whitespace();
            if (peek() != '"')
                return fail("expected object key");
            JsonValue keyValue;
            if (!parse_string(keyValue))
                return false;
            skip_whitespace();
            if (peek() != ':')
                return fail("expected ':' after object key");
            advance();
            skip_whitespace();
            JsonValue value;
            if (!parse_value(value))
                return false;
            if (out.find(keyValue.string) != nullptr)
                return fail("duplicate object key '" + keyValue.string + "'");
            out.object.emplace_back(std::move(keyValue.string), std::move(value));
            skip_whitespace();
            if (peek() == ',') {
                advance();
                continue;
            }
            if (peek() == '}') {
                advance();
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool parse_array(JsonValue& out) {
        out = JsonValue{};
        out.type = JsonValue::Type::Array;
        out.line = m_line;
        out.column = m_column;
        advance();
        skip_whitespace();
        if (peek() == ']') {
            advance();
            return true;
        }

        while (true) {
            skip_whitespace();
            JsonValue value;
            if (!parse_value(value))
                return false;
            out.array.push_back(std::move(value));
            skip_whitespace();
            if (peek() == ',') {
                advance();
                continue;
            }
            if (peek() == ']') {
                advance();
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool parse_string(JsonValue& out) {
        out = JsonValue{};
        out.type = JsonValue::Type::String;
        out.line = m_line;
        out.column = m_column;
        advance();
        std::string result;
        while (!at_end()) {
            const char character = peek();
            if (character == '"') {
                advance();
                out.string = std::move(result);
                return true;
            }
            if (static_cast<unsigned char>(character) < 0x20)
                return fail("control character in string");
            if (character != '\\') {
                result.push_back(character);
                advance();
                continue;
            }

            advance();
            if (at_end())
                return fail("unterminated escape sequence");
            const char escaped = peek();
            advance();
            switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                uint32_t codePoint = 0;
                if (!parse_hex4(codePoint))
                    return false;
                if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                    if (peek() != '\\')
                        return fail("missing low surrogate");
                    advance();
                    if (peek() != 'u')
                        return fail("missing low surrogate");
                    advance();
                    uint32_t low = 0;
                    if (!parse_hex4(low))
                        return false;
                    if (low < 0xDC00 || low > 0xDFFF)
                        return fail("invalid low surrogate");
                    codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
                    return fail("unexpected low surrogate");
                }
                append_utf8(result, codePoint);
                break;
            }
            default:
                return fail("invalid escape sequence");
            }
        }
        return fail("unterminated string");
    }

    bool parse_hex4(uint32_t& value) {
        value = 0;
        for (int i = 0; i < 4; ++i) {
            if (at_end())
                return fail("unterminated unicode escape");
            const char character = peek();
            int digit = -1;
            if (character >= '0' && character <= '9')
                digit = character - '0';
            else if (character >= 'a' && character <= 'f')
                digit = character - 'a' + 10;
            else if (character >= 'A' && character <= 'F')
                digit = character - 'A' + 10;
            if (digit < 0)
                return fail("invalid unicode escape");
            value = value * 16 + static_cast<uint32_t>(digit);
            advance();
        }
        return true;
    }

    bool parse_bool(JsonValue& out) {
        out = JsonValue{};
        out.type = JsonValue::Type::Bool;
        out.line = m_line;
        out.column = m_column;
        if (m_text.substr(m_pos, 4) == "true") {
            out.boolean = true;
            for (int i = 0; i < 4; ++i)
                advance();
            return true;
        }
        if (m_text.substr(m_pos, 5) == "false") {
            out.boolean = false;
            for (int i = 0; i < 5; ++i)
                advance();
            return true;
        }
        return fail("expected true or false");
    }

    bool parse_null(JsonValue& out) {
        out = JsonValue{};
        out.type = JsonValue::Type::Null;
        out.line = m_line;
        out.column = m_column;
        if (m_text.substr(m_pos, 4) != "null")
            return fail("expected null");
        for (int i = 0; i < 4; ++i)
            advance();
        return true;
    }

    bool parse_number(JsonValue& out) {
        out = JsonValue{};
        out.type = JsonValue::Type::Number;
        out.line = m_line;
        out.column = m_column;

        const std::size_t start = m_pos;
        if (peek() == '-')
            advance();
        while (!at_end() && peek() >= '0' && peek() <= '9')
            advance();
        if (!at_end() && peek() == '.') {
            advance();
            while (!at_end() && peek() >= '0' && peek() <= '9')
                advance();
        }
        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            advance();
            if (!at_end() && (peek() == '+' || peek() == '-'))
                advance();
            while (!at_end() && peek() >= '0' && peek() <= '9')
                advance();
        }

        const std::string buffer(m_text.substr(start, m_pos - start));
        char* end = nullptr;
        const double value = std::strtod(buffer.c_str(), &end);
        if (end == nullptr || *end != '\0' || !std::isfinite(value))
            return fail("invalid number");
        out.number = value;
        return true;
    }

    std::string_view m_text;
    JsonParseError& m_error;
    std::size_t m_pos = 0;
    std::size_t m_line = 1;
    std::size_t m_column = 1;
};

void write_string(std::ostringstream& stream, const std::string& value) {
    stream << '"';
    for (unsigned char character : value) {
        switch (character) {
        case '"': stream << "\\\""; break;
        case '\\': stream << "\\\\"; break;
        case '\b': stream << "\\b"; break;
        case '\f': stream << "\\f"; break;
        case '\n': stream << "\\n"; break;
        case '\r': stream << "\\r"; break;
        case '\t': stream << "\\t"; break;
        default:
            if (character < 0x20) {
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(character) << std::dec << std::setfill(' ');
            } else {
                stream << static_cast<char>(character);
            }
            break;
        }
    }
    stream << '"';
}

void write_value(std::ostringstream& stream, const JsonValue& value, int indent, int depth) {
    const std::string newline = indent > 0 ? "\n" : "";
    const std::string padding =
        indent > 0 ? std::string(static_cast<std::size_t>(depth + 1) * indent, ' ') : "";
    const std::string closing =
        indent > 0 ? std::string(static_cast<std::size_t>(depth) * indent, ' ') : "";

    switch (value.type) {
    case JsonValue::Type::Null:
        stream << "null";
        break;
    case JsonValue::Type::Bool:
        stream << (value.boolean ? "true" : "false");
        break;
    case JsonValue::Type::Number: {
        if (value.number == std::floor(value.number) && std::fabs(value.number) < 9.0e15) {
            stream << static_cast<long long>(value.number);
        } else {
            stream << std::setprecision(17) << value.number;
        }
        break;
    }
    case JsonValue::Type::String:
        write_string(stream, value.string);
        break;
    case JsonValue::Type::Array:
        if (value.array.empty()) {
            stream << "[]";
            break;
        }
        stream << "[" << newline;
        for (std::size_t i = 0; i < value.array.size(); ++i) {
            stream << padding;
            write_value(stream, value.array[i], indent, depth + 1);
            if (i + 1 < value.array.size())
                stream << ",";
            stream << newline;
        }
        stream << closing << "]";
        break;
    case JsonValue::Type::Object:
        if (value.object.empty()) {
            stream << "{}";
            break;
        }
        stream << "{" << newline;
        for (std::size_t i = 0; i < value.object.size(); ++i) {
            stream << padding;
            write_string(stream, value.object[i].first);
            stream << ":";
            if (indent > 0)
                stream << " ";
            write_value(stream, value.object[i].second, indent, depth + 1);
            if (i + 1 < value.object.size())
                stream << ",";
            stream << newline;
        }
        stream << closing << "}";
        break;
    }
}

} // namespace

const JsonValue* JsonValue::find(std::string_view key) const {
    for (const auto& entry : object) {
        if (entry.first == key)
            return &entry.second;
    }
    return nullptr;
}

JsonValue* JsonValue::find(std::string_view key) {
    for (auto& entry : object) {
        if (entry.first == key)
            return &entry.second;
    }
    return nullptr;
}

bool parse_json(std::string_view text, JsonValue& out, JsonParseError& error) {
    Parser parser(text, error);
    return parser.parse(out);
}

std::string write_json(const JsonValue& value, int indent) {
    std::ostringstream stream;
    write_value(stream, value, indent, 0);
    return stream.str();
}

} // namespace insys::asset
