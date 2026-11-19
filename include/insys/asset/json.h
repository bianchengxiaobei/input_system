#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace insys::asset {

struct JsonValue {
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;
    std::size_t line = 0;
    std::size_t column = 0;

    const JsonValue* find(std::string_view key) const;
    JsonValue* find(std::string_view key);
    bool is_null() const { return type == Type::Null; }
    bool is_object() const { return type == Type::Object; }
    bool is_array() const { return type == Type::Array; }
    bool is_string() const { return type == Type::String; }
    bool is_number() const { return type == Type::Number; }
    bool is_bool() const { return type == Type::Bool; }
};

struct JsonParseError {
    std::size_t line = 1;
    std::size_t column = 1;
    std::string message;
};

bool parse_json(std::string_view text, JsonValue& out, JsonParseError& error);
std::string write_json(const JsonValue& value, int indent = 2);

} // namespace insys::asset
