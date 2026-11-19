#include "insys/asset/asset_loader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

#include "insys/actions/action_map.h"
#include "insys/actions/composite.h"
#include "insys/actions/descriptor_parser.h"
#include "insys/actions/interaction.h"
#include "insys/actions/processor.h"
#include "insys/asset/json.h"
#include "insys/control_path.h"
#include "insys/string_utils.h"

namespace insys::asset {
namespace {

using JsonValue = insys::asset::JsonValue;

[[noreturn]] void fail(const std::string& context, const JsonValue& value,
                       const std::string& message) {
    throw AssetError(context + ": " + message, value.line, value.column);
}

void check_keys(const JsonValue& object, std::initializer_list<std::string_view> allowed,
                const std::string& context) {
    for (const auto& entry : object.object) {
        bool found = false;
        for (std::string_view key : allowed) {
            if (entry.first == key) {
                found = true;
                break;
            }
        }
        if (!found)
            fail(context, entry.second, "unknown field '" + entry.first + "'");
    }
}

const JsonValue& require_field(const JsonValue& object, std::string_view name,
                               const std::string& context) {
    const JsonValue* value = object.find(name);
    if (value == nullptr)
        fail(context, object, "missing required field '" + std::string(name) + "'");
    return *value;
}

std::string require_string(const JsonValue& object, std::string_view name,
                           const std::string& context) {
    const JsonValue& value = require_field(object, name, context);
    if (!value.is_string())
        fail(context + "." + std::string(name), value, "expected a string");
    return value.string;
}

std::string optional_string(const JsonValue& object, std::string_view name,
                            const std::string& context, std::string fallback = {}) {
    const JsonValue* value = object.find(name);
    if (value == nullptr)
        return fallback;
    if (!value->is_string())
        fail(context + "." + std::string(name), *value, "expected a string");
    return value->string;
}

bool optional_bool(const JsonValue& object, std::string_view name, const std::string& context,
                   bool fallback = false) {
    const JsonValue* value = object.find(name);
    if (value == nullptr)
        return fallback;
    if (!value->is_bool())
        fail(context + "." + std::string(name), *value, "expected a boolean");
    return value->boolean;
}

int optional_int(const JsonValue& object, std::string_view name, const std::string& context,
                 int fallback = 0) {
    const JsonValue* value = object.find(name);
    if (value == nullptr)
        return fallback;
    if (!value->is_number() || value->number != std::floor(value->number) ||
        value->number < static_cast<double>(std::numeric_limits<int>::min()) ||
        value->number > static_cast<double>(std::numeric_limits<int>::max())) {
        fail(context + "." + std::string(name), *value, "expected a 32-bit integer");
    }
    return static_cast<int>(value->number);
}

const JsonValue* optional_array(const JsonValue& object, std::string_view name,
                                const std::string& context) {
    const JsonValue* value = object.find(name);
    if (value != nullptr && !value->is_array())
        fail(context + "." + std::string(name), *value, "expected an array");
    return value;
}

Guid optional_id(const JsonValue& object, const std::string& context) {
    const JsonValue* value = object.find("id");
    if (value == nullptr)
        return Guid::create();
    if (!value->is_string())
        fail(context + ".id", *value, "expected a string");
    const Guid id = Guid::parse(value->string);
    if (!id.valid())
        fail(context + ".id", *value, "invalid GUID");
    return id;
}

ActionType parse_action_type(const std::string& text, const JsonValue& value,
                             const std::string& context) {
    if (equals_ignore_case(text, "value"))
        return ActionType::Value;
    if (equals_ignore_case(text, "button"))
        return ActionType::Button;
    if (equals_ignore_case(text, "passthrough") || equals_ignore_case(text, "pass-through"))
        return ActionType::PassThrough;
    fail(context + ".type", value, "unknown action type '" + text + "'");
}

void validate_interactions(const std::string& text, const JsonValue& value,
                           const std::string& context) {
    if (text.empty())
        return;
    for (const NameAndParameters& item : parse_list(text)) {
        if (!interaction_registry().has(item.name))
            fail(context, value, "unknown interaction '" + item.name + "'");
    }
}

void validate_processors(const std::string& text, const JsonValue& value,
                         const std::string& context) {
    if (text.empty())
        return;
    for (const NameAndParameters& item : parse_list(text)) {
        if (!make_processor(item.name, item.parameters))
            fail(context, value, "unknown processor '" + item.name + "'");
    }
}

void validate_path(const std::string& path, const JsonValue& value, const std::string& context) {
    if (path.empty())
        fail(context, value, "control path must not be empty");
    if (parse_path(path).empty())
        fail(context, value, "invalid control path '" + path + "'");
}

std::string qualified_context(const std::string& parent, std::string_view field,
                              std::size_t index) {
    std::string result = parent;
    if (!result.empty())
        result += ".";
    result += field;
    result += "[";
    result += std::to_string(index);
    result += "]";
    return result;
}

void parse_action(const JsonValue& value, ActionMap& map, const std::string& context) {
    if (!value.is_object())
        fail(context, value, "expected an object");
    check_keys(value, {"name", "id", "type", "control", "processors", "interactions",
                       "initialStateCheck", "priority"},
               context);

    const std::string name = require_string(value, "name", context);
    if (map.find_action(name) != nullptr)
        fail(context + ".name", value, "duplicate action '" + name + "'");

    ActionDef& action = map.add_action(name);
    action.id = optional_id(value, context);
    if (const JsonValue* type = value.find("type")) {
        if (!type->is_string())
            fail(context + ".type", *type, "expected a string");
        action.type = parse_action_type(type->string, *type, context);
    }
    action.expectedControlType = optional_string(value, "control", context);
    action.processors = optional_string(value, "processors", context);
    action.interactions = optional_string(value, "interactions", context);
    action.initialStateCheck = optional_bool(value, "initialStateCheck", context);
    action.priority = optional_int(value, "priority", context);

    if (const JsonValue* processors = value.find("processors"))
        validate_processors(action.processors, *processors, context);
    if (const JsonValue* interactions = value.find("interactions"))
        validate_interactions(action.interactions, *interactions, context);
}

void parse_binding(const JsonValue& value, ActionMap& map, const std::string& context) {
    if (!value.is_object())
        fail(context, value, "expected an object");
    check_keys(value, {"action", "id", "name", "path", "composite", "parts", "interactions",
                       "processors", "groups"},
               context);

    const std::string actionName = require_string(value, "action", context);
    ActionDef* action = map.find_action(actionName);
    if (action == nullptr) {
        const std::size_t slash = actionName.find('/');
        if (slash != std::string::npos)
            action = map.find_action(actionName.substr(slash + 1));
    }
    if (action == nullptr)
        fail(context + ".action", value, "unknown action '" + actionName + "'");

    const JsonValue* path = value.find("path");
    const JsonValue* composite = value.find("composite");
    const JsonValue* parts = value.find("parts");
    if (path != nullptr && composite != nullptr)
        fail(context, value, "binding cannot have both 'path' and 'composite'");
    if (composite == nullptr && parts != nullptr)
        fail(context + ".parts", *parts, "'parts' requires 'composite'");
    if (path == nullptr && composite == nullptr)
        fail(context, value, "binding requires 'path' or 'composite'");

    const std::string interactions = optional_string(value, "interactions", context);
    const std::string processors = optional_string(value, "processors", context);
    const std::string groups = optional_string(value, "groups", context);
    if (const JsonValue* interactionsValue = value.find("interactions"))
        validate_interactions(interactions, *interactionsValue, context);
    if (const JsonValue* processorsValue = value.find("processors"))
        validate_processors(processors, *processorsValue, context);

    if (composite == nullptr) {
        if (!path->is_string())
            fail(context + ".path", *path, "expected a string");
        validate_path(path->string, *path, context + ".path");
        Binding& binding = map.add_binding(action->name, path->string,
                                           optional_string(value, "name", context));
        binding.id = optional_id(value, context);
        binding.interactions = interactions;
        binding.processors = processors;
        binding.groups = groups;
        return;
    }

    if (!composite->is_string())
        fail(context + ".composite", *composite, "expected a string");
    const NameAndParameters descriptor = parse_item(composite->string);
    std::unique_ptr<IComposite> compositeValue =
        make_composite(descriptor.name, descriptor.parameters);
    if (!compositeValue)
        fail(context + ".composite", *composite,
             "unknown composite '" + descriptor.name + "'");
    if (parts == nullptr || !parts->is_object())
        fail(context + ".parts", parts != nullptr ? *parts : *composite,
             "composite binding requires a 'parts' object");
    if (parts->object.empty())
        fail(context + ".parts", *parts, "composite binding requires at least one part");

    Binding& binding = map.add_composite_binding(action->name, composite->string,
                                                 optional_string(value, "name", context));
    binding.id = optional_id(value, context);
    binding.interactions = interactions;
    binding.processors = processors;
    binding.groups = groups;

    int currentPart = 0;
    for (const auto& part : parts->object) {
        if (!part.second.is_string())
            fail(context + ".parts." + part.first, part.second, "expected a string");
        if (compositeValue->assign_part(part.first, currentPart) < 0)
            fail(context + ".parts." + part.first, part.second,
                 "unknown composite part '" + part.first + "'");
        validate_path(part.second.string, part.second, context + ".parts." + part.first);
        Binding& partBinding = map.add_composite_part(action->name, part.first,
                                                     part.second.string);
        partBinding.interactions = interactions;
        partBinding.processors = processors;
        partBinding.groups = groups;
    }
}

void parse_map(const JsonValue& value, ActionAsset& asset, const std::string& context) {
    if (!value.is_object())
        fail(context, value, "expected an object");
    check_keys(value, {"name", "id", "actions", "bindings"}, context);

    const std::string name = require_string(value, "name", context);
    for (const ActionMap& existing : asset.maps) {
        if (equals_ignore_case(existing.name, name))
            fail(context + ".name", value, "duplicate action map '" + name + "'");
    }

    ActionMap& map = asset.add_map(name);
    map.id = optional_id(value, context);

    if (const JsonValue* actions = optional_array(value, "actions", context)) {
        for (std::size_t i = 0; i < actions->array.size(); ++i) {
            parse_action(actions->array[i], map,
                         qualified_context(context, "actions", i));
        }
    }
    if (const JsonValue* bindings = optional_array(value, "bindings", context)) {
        for (std::size_t i = 0; i < bindings->array.size(); ++i) {
            parse_binding(bindings->array[i], map,
                          qualified_context(context, "bindings", i));
        }
    }
}

ControlScheme parse_scheme(const JsonValue& value, const std::string& context) {
    if (!value.is_object())
        fail(context, value, "expected an object");
    check_keys(value, {"name", "id", "bindingGroup", "devices", "isOptional"}, context);

    ControlScheme scheme;
    scheme.name = require_string(value, "name", context);
    scheme.id = optional_id(value, context);
    scheme.bindingGroup = optional_string(value, "bindingGroup", context);
    scheme.isOptional = optional_bool(value, "isOptional", context);

    const JsonValue& devices = require_field(value, "devices", context);
    if (!devices.is_array() || devices.array.empty())
        fail(context + ".devices", devices, "expected a non-empty array");
    for (std::size_t i = 0; i < devices.array.size(); ++i) {
        if (!devices.array[i].is_string())
            fail(qualified_context(context, "devices", i), devices.array[i],
                 "expected a string");
        scheme.devices.push_back(devices.array[i].string);
    }
    return scheme;
}

ActionAsset build_asset(const JsonValue& root) {
    if (!root.is_object())
        fail("<root>", root, "expected an object");
    check_keys(root, {"version", "name", "maps", "schemes"}, "<root>");

    const JsonValue& version = require_field(root, "version", "<root>");
    if (!version.is_number() || version.number != 1.0)
        fail("<root>.version", version, "unsupported asset version (expected 1)");

    ActionAsset asset;
    const JsonValue* name = root.find("name");
    if (name != nullptr) {
        if (!name->is_string())
            fail("<root>.name", *name, "expected a string");
        asset.name = name->string;
    }

    const JsonValue& maps = require_field(root, "maps", "<root>");
    if (!maps.is_array())
        fail("<root>.maps", maps, "expected an array");
    for (std::size_t i = 0; i < maps.array.size(); ++i)
        parse_map(maps.array[i], asset, qualified_context("<root>", "maps", i));

    if (const JsonValue* schemes = optional_array(root, "schemes", "<root>")) {
        for (std::size_t i = 0; i < schemes->array.size(); ++i) {
            ControlScheme scheme =
                parse_scheme(schemes->array[i], qualified_context("<root>", "schemes", i));
            for (const ControlScheme& existing : asset.controlSchemes) {
                if (equals_ignore_case(existing.name, scheme.name))
                    fail(qualified_context("<root>", "schemes", i),
                         schemes->array[i], "duplicate control scheme '" + scheme.name + "'");
            }
            asset.controlSchemes.push_back(std::move(scheme));
        }
    }
    return asset;
}

JsonValue make_object() {
    JsonValue value;
    value.type = JsonValue::Type::Object;
    return value;
}

JsonValue make_array() {
    JsonValue value;
    value.type = JsonValue::Type::Array;
    return value;
}

JsonValue make_string(std::string value) {
    JsonValue result;
    result.type = JsonValue::Type::String;
    result.string = std::move(value);
    return result;
}

JsonValue make_bool(bool value) {
    JsonValue result;
    result.type = JsonValue::Type::Bool;
    result.boolean = value;
    return result;
}

JsonValue make_number(double value) {
    JsonValue result;
    result.type = JsonValue::Type::Number;
    result.number = value;
    return result;
}

void add(JsonValue& object, std::string key, JsonValue value) {
    object.object.emplace_back(std::move(key), std::move(value));
}

std::string action_type_name(ActionType type) {
    switch (type) {
    case ActionType::Value: return "value";
    case ActionType::PassThrough: return "passthrough";
    case ActionType::Button:
    default: return "button";
    }
}

JsonValue serialize_action(const ActionDef& action) {
    JsonValue result = make_object();
    add(result, "name", make_string(action.name));
    add(result, "id", make_string(action.id.to_string()));
    add(result, "type", make_string(action_type_name(action.type)));
    if (!action.expectedControlType.empty())
        add(result, "control", make_string(action.expectedControlType));
    if (!action.processors.empty())
        add(result, "processors", make_string(action.processors));
    if (!action.interactions.empty())
        add(result, "interactions", make_string(action.interactions));
    if (action.initialStateCheck)
        add(result, "initialStateCheck", make_bool(true));
    if (action.priority != 0)
        add(result, "priority", make_number(action.priority));
    return result;
}

JsonValue serialize_binding_common(const Binding& binding, bool composite) {
    JsonValue result = make_object();
    add(result, "action", make_string(binding.action));
    add(result, "id", make_string(binding.id.to_string()));
    if (!binding.name.empty())
        add(result, "name", make_string(binding.name));
    if (composite)
        add(result, "composite", make_string(binding.path));
    else
        add(result, "path", make_string(binding.path));
    if (!binding.interactions.empty())
        add(result, "interactions", make_string(binding.interactions));
    if (!binding.processors.empty())
        add(result, "processors", make_string(binding.processors));
    if (!binding.groups.empty())
        add(result, "groups", make_string(binding.groups));
    return result;
}

JsonValue serialize_bindings(const ActionMap& map) {
    JsonValue array = make_array();
    for (std::size_t i = 0; i < map.bindings.size(); ++i) {
        const Binding& binding = map.bindings[i];
        if (binding.isPartOfComposite)
            continue;
        JsonValue item = serialize_binding_common(binding, binding.isComposite);
        if (binding.isComposite) {
            JsonValue parts = make_object();
            for (std::size_t j = i + 1; j < map.bindings.size(); ++j) {
                const Binding& part = map.bindings[j];
                if (!part.isPartOfComposite)
                    break;
                add(parts, part.name, make_string(part.path));
            }
            add(item, "parts", std::move(parts));
        }
        array.array.push_back(std::move(item));
    }
    return array;
}

JsonValue serialize_map(const ActionMap& map) {
    JsonValue result = make_object();
    add(result, "name", make_string(map.name));
    add(result, "id", make_string(map.id.to_string()));
    JsonValue actions = make_array();
    for (const ActionDef& action : map.actions)
        actions.array.push_back(serialize_action(action));
    add(result, "actions", std::move(actions));
    add(result, "bindings", serialize_bindings(map));
    return result;
}

JsonValue serialize_scheme(const ControlScheme& scheme) {
    JsonValue result = make_object();
    add(result, "name", make_string(scheme.name));
    add(result, "id", make_string(scheme.id.to_string()));
    if (!scheme.bindingGroup.empty())
        add(result, "bindingGroup", make_string(scheme.bindingGroup));
    JsonValue devices = make_array();
    for (const std::string& device : scheme.devices)
        devices.array.push_back(make_string(device));
    add(result, "devices", std::move(devices));
    if (scheme.isOptional)
        add(result, "isOptional", make_bool(true));
    return result;
}

} // namespace

ActionAsset AssetLoader::load_from_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw AssetError("cannot open asset file '" + path.string() + "'");
    std::ostringstream buffer;
    buffer << stream.rdbuf();

    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    try {
        if (extension == ".json")
            return load_from_json(buffer.str());
        if (extension == ".insysb")
            return load_from_binary(buffer.str());
        throw AssetError("unsupported asset extension '" + extension + "'");
    } catch (const AssetError& error) {
        throw AssetError(path.string() + ": " + error.what(), error.line(), error.column());
    }
}

ActionAsset AssetLoader::load_from_json(std::string_view json) {
    JsonValue root;
    JsonParseError parseError;
    if (!parse_json(json, root, parseError))
        throw AssetError(parseError.message + " at line " + std::to_string(parseError.line) +
                             ", column " + std::to_string(parseError.column),
                         parseError.line, parseError.column);
    return build_asset(root);
}

std::string AssetLoader::to_json(const ActionAsset& asset) {
    JsonValue root = make_object();
    add(root, "version", make_number(1));
    if (!asset.name.empty())
        add(root, "name", make_string(asset.name));

    JsonValue maps = make_array();
    for (const ActionMap& map : asset.maps)
        maps.array.push_back(serialize_map(map));
    add(root, "maps", std::move(maps));

    if (!asset.controlSchemes.empty()) {
        JsonValue schemes = make_array();
        for (const ControlScheme& scheme : asset.controlSchemes)
            schemes.array.push_back(serialize_scheme(scheme));
        add(root, "schemes", std::move(schemes));
    }
    return write_json(root, 2);
}

} // namespace insys::asset
