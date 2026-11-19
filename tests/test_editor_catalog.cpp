#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "insys/asset/json.h"
#include "insys/editor/editor_catalog.h"

using namespace insys;

namespace {

const editor::CatalogLayout* find_layout(const editor::EditorCatalog& catalog,
                                         const std::string& name) {
    const auto it = std::find_if(catalog.layouts.begin(), catalog.layouts.end(),
                                 [&](const editor::CatalogLayout& layout) {
                                     return layout.name == name;
                                 });
    return it != catalog.layouts.end() ? &*it : nullptr;
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

const editor::CatalogComposite* find_composite(const editor::EditorCatalog& catalog,
                                               const std::string& name) {
    const auto it = std::find_if(catalog.composites.begin(), catalog.composites.end(),
                                 [&](const editor::CatalogComposite& composite) {
                                     return composite.name == name;
                                 });
    return it != catalog.composites.end() ? &*it : nullptr;
}

} // namespace

TEST_CASE("Editor catalog exposes layouts controls and registries") {
    const editor::EditorCatalog catalog = editor::build_editor_catalog();

    REQUIRE(!catalog.layouts.empty());
    REQUIRE(contains(catalog.devices, "Keyboard"));
    REQUIRE(contains(catalog.devices, "Gamepad"));
    REQUIRE(contains(catalog.controlTypes, "Any"));
    REQUIRE(contains(catalog.controlTypes, "Button"));
    REQUIRE(contains(catalog.controlTypes, "Vector2"));
    REQUIRE(contains(catalog.controlTypes, "Key"));

    const editor::CatalogLayout* gamepad = find_layout(catalog, "Gamepad");
    REQUIRE(gamepad != nullptr);
    const auto button = std::find_if(
        gamepad->controls.begin(), gamepad->controls.end(),
        [](const editor::CatalogControl& control) {
            return control.name == "buttonSouth";
        });
    REQUIRE(button != gamepad->controls.end());
    REQUIRE(button->path == "<Gamepad>/buttonSouth");
    REQUIRE(button->layout == "Button");

    const editor::CatalogLayout* keyboard = find_layout(catalog, "Keyboard");
    REQUIRE(keyboard != nullptr);
    REQUIRE(!keyboard->controls.empty());

    const editor::CatalogComposite* axis = find_composite(catalog, "Axis");
    REQUIRE(axis != nullptr);
    REQUIRE(contains(axis->parts, "negative"));
    REQUIRE(contains(axis->parts, "positive"));
    REQUIRE(!axis->parameters.empty());

    const editor::CatalogComposite* modifiers = find_composite(catalog, "TwoModifiers");
    REQUIRE(modifiers != nullptr);
    REQUIRE(contains(modifiers->parts, "modifier1"));
    REQUIRE(contains(modifiers->parts, "modifier2"));
    REQUIRE(contains(modifiers->parts, "binding"));

    const auto tap = std::find_if(
        catalog.interactions.begin(), catalog.interactions.end(),
        [](const editor::CatalogNamedItem& item) { return item.name == "Tap"; });
    REQUIRE(tap != catalog.interactions.end());
    REQUIRE(!tap->parameters.empty());

    const auto deadzone = std::find_if(
        catalog.processors.begin(), catalog.processors.end(),
        [](const editor::CatalogNamedItem& item) { return item.name == "AxisDeadzone"; });
    REQUIRE(deadzone != catalog.processors.end());
}

TEST_CASE("Editor catalog serializes to JSON") {
    const editor::EditorCatalog catalog = editor::build_editor_catalog();
    const asset::JsonValue json = editor::catalog_to_json(catalog);
    const std::string text = asset::write_json(json);
    REQUIRE(text.find("\"layouts\"") != std::string::npos);
    REQUIRE(text.find("\"controlTypes\"") != std::string::npos);
    REQUIRE(text.find("\"OneModifier\"") != std::string::npos);
    REQUIRE(text.find("\"AxisDeadzone\"") != std::string::npos);

    asset::JsonValue parsed;
    asset::JsonParseError error;
    REQUIRE(asset::parse_json(text, parsed, error));
    const asset::JsonValue* layouts = parsed.find("layouts");
    REQUIRE(layouts != nullptr);
    REQUIRE(layouts->is_array());
    REQUIRE(!layouts->array.empty());
}
