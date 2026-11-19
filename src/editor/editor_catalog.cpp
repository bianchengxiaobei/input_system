#include "insys/editor/editor_catalog.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "insys/actions/interaction.h"
#include "insys/layout_registry.h"
#include "insys/string_utils.h"

namespace insys::editor {
namespace {

using JsonValue = asset::JsonValue;

bool less_case_insensitive(std::string_view left, std::string_view right) {
    const std::size_t count = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned char a = static_cast<unsigned char>(left[i]);
        const unsigned char b = static_cast<unsigned char>(right[i]);
        const int lowerA = std::tolower(a);
        const int lowerB = std::tolower(b);
        if (lowerA != lowerB)
            return lowerA < lowerB;
    }
    return left.size() < right.size();
}

CatalogParameter parameter(std::string name, std::string displayName, std::string type,
                           std::string defaultValue,
                           std::vector<std::string> options = {}) {
    CatalogParameter result;
    result.name = std::move(name);
    result.displayName = std::move(displayName);
    result.type = std::move(type);
    result.defaultValue = std::move(defaultValue);
    result.options = std::move(options);
    return result;
}

std::vector<CatalogNamedItem> make_interactions() {
    std::vector<CatalogNamedItem> result;
    InteractionRegistry& registry = interaction_registry();
    for (const std::string& name : registry.names()) {
        CatalogNamedItem item;
        item.name = name;
        item.displayName = name;
        if (equals_ignore_case(name, "Tap")) {
            item.parameters.push_back(parameter("duration", "Duration", "float", "0.2"));
            item.parameters.push_back(parameter("pressPoint", "Press Point", "float", "0.5"));
        } else if (equals_ignore_case(name, "Hold")) {
            item.parameters.push_back(parameter("duration", "Duration", "float", "0.4"));
            item.parameters.push_back(parameter("pressPoint", "Press Point", "float", "0.5"));
        } else if (equals_ignore_case(name, "SlowTap")) {
            item.parameters.push_back(parameter("duration", "Duration", "float", "0.5"));
            item.parameters.push_back(parameter("pressPoint", "Press Point", "float", "0.5"));
        } else if (equals_ignore_case(name, "Press")) {
            item.parameters.push_back(
                parameter("behavior", "Behavior", "enum", "0",
                          {"Press Only", "Release Only", "Press And Release"}));
            item.parameters.push_back(parameter("pressPoint", "Press Point", "float", "0.5"));
        } else if (equals_ignore_case(name, "MultiTap")) {
            item.parameters.push_back(parameter("tapCount", "Tap Count", "int", "2"));
            item.parameters.push_back(parameter("tapTime", "Tap Time", "float", "0.2"));
            item.parameters.push_back(parameter("tapDelay", "Tap Delay", "float", "0.75"));
            item.parameters.push_back(parameter("pressPoint", "Press Point", "float", "0.5"));
        }
        result.push_back(std::move(item));
    }
    return result;
}

std::vector<CatalogNamedItem> make_processors() {
    const char* const names[] = {
        "Invert",
        "InvertVector2",
        "InvertVector3",
        "Scale",
        "ScaleVector2",
        "ScaleVector3",
        "Normalize",
        "NormalizeVector2",
        "NormalizeVector3",
        "Clamp",
        "AxisDeadzone",
        "StickDeadzone",
        "CompensateDirection",
        "CompensateRotation",
        "AndroidCompensateDirection",
        "AndroidCompensateRotation",
        "Smooth",
        "SmoothVector2",
        "SmoothVector3",
    };

    std::vector<CatalogNamedItem> result;
    for (const char* name : names) {
        CatalogNamedItem item;
        item.name = name;
        item.displayName = name;
        if (equals_ignore_case(name, "Scale")) {
            item.parameters.push_back(parameter("factor", "Factor", "float", "1"));
        } else if (equals_ignore_case(name, "ScaleVector2") ||
                   equals_ignore_case(name, "ScaleVector3")) {
            item.parameters.push_back(parameter("x", "X", "float", "1"));
            item.parameters.push_back(parameter("y", "Y", "float", "1"));
            if (equals_ignore_case(name, "ScaleVector3"))
                item.parameters.push_back(parameter("z", "Z", "float", "1"));
        } else if (equals_ignore_case(name, "Clamp")) {
            item.parameters.push_back(parameter("min", "Min", "float", "-1"));
            item.parameters.push_back(parameter("max", "Max", "float", "1"));
        } else if (equals_ignore_case(name, "AxisDeadzone")) {
            item.parameters.push_back(parameter("min", "Min", "float", "0.125"));
            item.parameters.push_back(parameter("max", "Max", "float", "0.925"));
        } else if (equals_ignore_case(name, "StickDeadzone")) {
            item.parameters.push_back(parameter("min", "Min", "float", "0.125"));
            item.parameters.push_back(parameter("max", "Max", "float", "0.925"));
        } else if (equals_ignore_case(name, "Smooth")) {
            item.parameters.push_back(parameter("duration", "Duration", "float", "0"));
        } else if (equals_ignore_case(name, "SmoothVector2") ||
                   equals_ignore_case(name, "SmoothVector3")) {
            item.parameters.push_back(parameter("duration", "Duration", "float", "0"));
        }
        result.push_back(std::move(item));
    }
    return result;
}

std::vector<CatalogComposite> make_composites() {
    std::vector<CatalogComposite> result;

    CatalogComposite axis;
    axis.name = "Axis";
    axis.displayName = "Axis";
    axis.parts = {"negative", "positive"};
    axis.parameters = {
        parameter("minValue", "Min Value", "float", "-1"),
        parameter("maxValue", "Max Value", "float", "1"),
        parameter("whichSideWins", "Which Side Wins", "enum", "0",
                  {"Neither", "Positive", "Negative"}),
    };
    result.push_back(std::move(axis));

    CatalogComposite vector2;
    vector2.name = "Vector2";
    vector2.displayName = "Vector 2";
    vector2.parts = {"up", "down", "left", "right"};
    vector2.parameters = {
        parameter("mode", "Mode", "enum", "0",
                  {"Digital Normalized", "Digital", "Analog"}),
    };
    result.push_back(std::move(vector2));

    CatalogComposite vector3;
    vector3.name = "Vector3";
    vector3.displayName = "Vector 3";
    vector3.parts = {"up", "down", "left", "right", "forward", "backward"};
    vector3.parameters = {
        parameter("mode", "Mode", "enum", "0",
                  {"Digital Normalized", "Digital", "Analog"}),
    };
    result.push_back(std::move(vector3));

    CatalogComposite oneModifier;
    oneModifier.name = "OneModifier";
    oneModifier.displayName = "One Modifier";
    oneModifier.parts = {"modifier", "binding"};
    oneModifier.parameters = {
        parameter("modifiersOrder", "Modifiers Order", "enum", "0",
                  {"Default", "Ordered", "Unordered"}),
        parameter("overrideModifiersNeedToBePressedFirst",
                  "Override Press Order", "bool", "false"),
    };
    result.push_back(std::move(oneModifier));

    CatalogComposite twoModifiers;
    twoModifiers.name = "TwoModifiers";
    twoModifiers.displayName = "Two Modifiers";
    twoModifiers.parts = {"modifier1", "modifier2", "binding"};
    twoModifiers.parameters = oneModifier.parameters;
    result.push_back(std::move(twoModifiers));

    return result;
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

JsonValue strings_json(const std::vector<std::string>& strings) {
    JsonValue result = make_array();
    for (const std::string& value : strings)
        result.array.push_back(make_string(value));
    return result;
}

JsonValue parameter_json(const CatalogParameter& value) {
    JsonValue result = make_object();
    add(result, "name", make_string(value.name));
    add(result, "displayName", make_string(value.displayName));
    add(result, "type", make_string(value.type));
    add(result, "default", make_string(value.defaultValue));
    if (!value.options.empty())
        add(result, "options", strings_json(value.options));
    return result;
}

JsonValue parameters_json(const std::vector<CatalogParameter>& parameters) {
    JsonValue result = make_array();
    for (const CatalogParameter& value : parameters)
        result.array.push_back(parameter_json(value));
    return result;
}

JsonValue named_item_json(const CatalogNamedItem& item) {
    JsonValue result = make_object();
    add(result, "name", make_string(item.name));
    add(result, "displayName", make_string(item.displayName));
    add(result, "parameters", parameters_json(item.parameters));
    return result;
}

JsonValue control_json(const CatalogControl& control) {
    JsonValue result = make_object();
    add(result, "name", make_string(control.name));
    add(result, "path", make_string(control.path));
    add(result, "layout", make_string(control.layout));
    add(result, "displayName", make_string(control.displayName));
    add(result, "usages", strings_json(control.usages));
    add(result, "aliases", strings_json(control.aliases));
    return result;
}

JsonValue layout_json(const CatalogLayout& layout) {
    JsonValue result = make_object();
    add(result, "name", make_string(layout.name));
    add(result, "displayName", make_string(layout.displayName));
    if (!layout.extends.empty())
        add(result, "extends", make_string(layout.extends));
    JsonValue controls = make_array();
    for (const CatalogControl& control : layout.controls)
        controls.array.push_back(control_json(control));
    add(result, "controls", std::move(controls));
    return result;
}

JsonValue composite_json(const CatalogComposite& composite) {
    JsonValue result = make_object();
    add(result, "name", make_string(composite.name));
    add(result, "displayName", make_string(composite.displayName));
    add(result, "parts", strings_json(composite.parts));
    add(result, "parameters", parameters_json(composite.parameters));
    return result;
}

std::string control_type_name(std::string value) {
    if (value.empty())
        return "Axis";
    return value;
}

} // namespace

EditorCatalog build_editor_catalog() {
    EditorCatalog catalog;
    LayoutRegistry& registry = default_layout_registry();

    std::set<std::string, decltype(&less_case_insensitive)> controlTypes(
        &less_case_insensitive);
    controlTypes.insert("Any");

    std::vector<std::string> layoutNames = registry.list();
    for (const std::string& name : layoutNames) {
        Layout* layout = registry.find(name);
        if (layout == nullptr)
            continue;

        CatalogLayout info;
        info.name = layout->name;
        info.displayName = layout->displayName.empty() ? layout->name
                                                       : layout->displayName;
        info.extends = layout->extends;

        for (const ControlItem& item : layout->controls) {
            if (item.name.empty())
                continue;
            CatalogControl control;
            control.name = item.name;
            control.path = "<" + layout->name + ">/" + item.name;
            control.layout = control_type_name(item.layout);
            control.displayName = item.displayName.empty() ? item.name
                                                            : item.displayName;
            control.usages = item.usages;
            control.aliases = item.aliases;
            info.controls.push_back(std::move(control));
            controlTypes.insert(control_type_name(item.layout));
        }

        catalog.devices.push_back(layout->name);
        catalog.layouts.push_back(std::move(info));
    }

    std::sort(catalog.layouts.begin(), catalog.layouts.end(),
              [](const CatalogLayout& left, const CatalogLayout& right) {
                  return less_case_insensitive(left.name, right.name);
              });
    std::sort(catalog.devices.begin(), catalog.devices.end(),
              [](const std::string& left, const std::string& right) {
                  return less_case_insensitive(left, right);
              });

    for (std::string name : controlTypes)
        catalog.controlTypes.push_back(std::move(name));

    catalog.composites = make_composites();
    catalog.interactions = make_interactions();
    catalog.processors = make_processors();
    return catalog;
}

asset::JsonValue catalog_to_json(const EditorCatalog& catalog) {
    JsonValue result = make_object();

    JsonValue layouts = make_array();
    for (const CatalogLayout& layout : catalog.layouts)
        layouts.array.push_back(layout_json(layout));
    add(result, "layouts", std::move(layouts));

    add(result, "devices", strings_json(catalog.devices));
    add(result, "controlTypes", strings_json(catalog.controlTypes));

    JsonValue composites = make_array();
    for (const CatalogComposite& composite : catalog.composites)
        composites.array.push_back(composite_json(composite));
    add(result, "composites", std::move(composites));

    JsonValue interactions = make_array();
    for (const CatalogNamedItem& item : catalog.interactions)
        interactions.array.push_back(named_item_json(item));
    add(result, "interactions", std::move(interactions));

    JsonValue processors = make_array();
    for (const CatalogNamedItem& item : catalog.processors)
        processors.array.push_back(named_item_json(item));
    add(result, "processors", std::move(processors));

    return result;
}

} // namespace insys::editor
