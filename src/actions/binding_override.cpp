#include "insys/actions/binding_override.h"

#include <cstddef>
#include <utility>

#include "insys/actions/action_map.h"
#include "insys/asset/json.h"
#include "insys/string_utils.h"

namespace insys {
namespace {

using JsonValue = insys::asset::JsonValue;

[[noreturn]] void fail(const std::string& message) {
    throw BindingOverrideError(message);
}

bool find_binding_by_id(ActionAsset& asset, const Guid& id, ActionMap*& map,
                        Binding*& binding) {
    for (ActionMap& candidateMap : asset.maps) {
        for (Binding& candidate : candidateMap.bindings) {
            if (candidate.id == id) {
                map = &candidateMap;
                binding = &candidate;
                return true;
            }
        }
    }
    return false;
}

bool find_binding_by_action(ActionAsset& asset, const std::string& actionPath, ActionMap*& map,
                            Binding*& binding) {
    std::string mapName;
    std::string actionName = actionPath;
    const std::size_t slash = actionPath.find('/');
    if (slash != std::string::npos) {
        mapName = actionPath.substr(0, slash);
        actionName = actionPath.substr(slash + 1);
    }

    for (ActionMap& candidateMap : asset.maps) {
        if (!mapName.empty() && !equals_ignore_case(candidateMap.name, mapName))
            continue;
        ActionDef* action = candidateMap.find_action(actionName);
        if (action == nullptr)
            continue;
        for (Binding& candidate : candidateMap.bindings) {
            if (!candidate.isPartOfComposite &&
                equals_ignore_case(candidate.action, action->name)) {
                map = &candidateMap;
                binding = &candidate;
                return true;
            }
        }
    }
    return false;
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

void add(JsonValue& object, std::string key, JsonValue value) {
    object.object.emplace_back(std::move(key), std::move(value));
}

} // namespace

std::string save_binding_overrides(const ActionAsset& asset) {
    JsonValue root = make_object();
    JsonValue bindings = make_array();

    for (const ActionMap& map : asset.maps) {
        for (const Binding& binding : map.bindings) {
            if (binding.overridePath.empty() && binding.overrideInteractions.empty() &&
                binding.overrideProcessors.empty())
                continue;

            JsonValue item = make_object();
            std::string actionPath = map.name;
            if (!binding.action.empty()) {
                if (!actionPath.empty())
                    actionPath += "/";
                actionPath += binding.action;
            }
            add(item, "action", make_string(actionPath));
            add(item, "id", make_string(binding.id.to_string()));
            if (!binding.overridePath.empty())
                add(item, "path", make_string(binding.overridePath));
            if (!binding.overrideInteractions.empty())
                add(item, "interactions", make_string(binding.overrideInteractions));
            if (!binding.overrideProcessors.empty())
                add(item, "processors", make_string(binding.overrideProcessors));
            bindings.array.push_back(std::move(item));
        }
    }

    add(root, "bindings", std::move(bindings));
    return asset::write_json(root, 2);
}

void load_binding_overrides(ActionAsset& asset, std::string_view json,
                            std::vector<std::string>* warnings) {
    JsonValue root;
    asset::JsonParseError parseError;
    if (!asset::parse_json(json, root, parseError)) {
        fail("invalid binding override JSON at line " + std::to_string(parseError.line) +
             ", column " + std::to_string(parseError.column) + ": " + parseError.message);
    }
    if (!root.is_object())
        fail("binding override JSON must be an object");

    const JsonValue* bindings = root.find("bindings");
    if (bindings == nullptr || !bindings->is_array())
        fail("binding override JSON requires a 'bindings' array");

    for (std::size_t i = 0; i < bindings->array.size(); ++i) {
        const JsonValue& entry = bindings->array[i];
        if (!entry.is_object())
            fail("binding override entry " + std::to_string(i) + " must be an object");
        for (const auto& field : entry.object) {
            if (field.first != "action" && field.first != "id" && field.first != "path" &&
                field.first != "interactions" && field.first != "processors") {
                fail("binding override entry " + std::to_string(i) +
                     " contains unknown field '" + field.first + "'");
            }
        }

        ActionMap* map = nullptr;
        Binding* binding = nullptr;
        bool found = false;

        if (const JsonValue* idValue = entry.find("id")) {
            if (!idValue->is_string())
                fail("binding override 'id' must be a string");
            const Guid id = Guid::parse(idValue->string);
            if (id.valid())
                found = find_binding_by_id(asset, id, map, binding);
        }

        if (!found) {
            const JsonValue* actionValue = entry.find("action");
            if (actionValue != nullptr) {
                if (!actionValue->is_string())
                    fail("binding override 'action' must be a string");
                found = find_binding_by_action(asset, actionValue->string, map, binding);
            }
        }

        if (!found) {
            if (warnings != nullptr)
                warnings->push_back("binding override " + std::to_string(i) +
                                    ": no matching binding");
            continue;
        }

        if (const JsonValue* path = entry.find("path")) {
            if (!path->is_string())
                fail("binding override 'path' must be a string");
            binding->overridePath = path->string;
        }
        if (const JsonValue* interactions = entry.find("interactions")) {
            if (!interactions->is_string())
                fail("binding override 'interactions' must be a string");
            binding->overrideInteractions = interactions->string;
        }
        if (const JsonValue* processors = entry.find("processors")) {
            if (!processors->is_string())
                fail("binding override 'processors' must be a string");
            binding->overrideProcessors = processors->string;
        }
    }

    if (asset.state() != nullptr)
        asset.re_resolve();
}

} // namespace insys
