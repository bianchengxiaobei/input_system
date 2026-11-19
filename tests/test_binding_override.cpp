#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "action_test_helpers.h"
#include "insys/actions/action_state.h"
#include "insys/actions/binding_override.h"
#include "insys/asset/asset_loader.h"

using namespace insys;

namespace {

const char* kOverrideAsset = R"({
  "version": 1,
  "name": "default",
  "maps": [{
    "name": "Player",
    "actions": [{"name": "Jump"}, {"name": "Fire"}],
    "bindings": [
      {"action": "Jump", "path": "<Keyboard>/space"},
      {"action": "Jump", "path": "<Gamepad>/buttonSouth"},
      {"action": "Fire", "path": "<Mouse>/leftButton"}
    ]
  }]
})";

} // namespace

TEST_CASE("Binding overrides save and load by binding id") {
    insys_test::TestRuntime runtime;
    Manager manager{runtime};
    Keyboard* keyboard = insys_test::add_keyboard(manager);

    ActionAsset asset = asset::AssetLoader::load_from_json(kOverrideAsset);
    Binding& jump = asset.maps[0].bindings[0];
    jump.overridePath = "<Keyboard>/j";
    jump.overrideInteractions = "Tap";
    asset.maps[0].bindings[2].overrideProcessors = "Scale(factor=2)";

    const std::string saved = save_binding_overrides(asset);
    REQUIRE(saved.find("Keyboard") != std::string::npos);
    REQUIRE(saved.find(jump.id.to_string()) != std::string::npos);

    {
        ActionAsset loaded = asset::AssetLoader::load_from_json(kOverrideAsset);
        std::vector<std::string> warnings;
        load_binding_overrides(loaded, saved, &warnings);
        REQUIRE(warnings.empty());
        REQUIRE(loaded.maps[0].bindings[0].overridePath == "<Keyboard>/j");
        REQUIRE(loaded.maps[0].bindings[0].overrideInteractions == "Tap");
        REQUIRE(loaded.maps[0].bindings[2].overrideProcessors == "Scale(factor=2)");

        loaded.enable(manager);
        ActionDef* jumpAction = loaded.find_action("Jump");
        insys_test::set_keys(manager, keyboard, {Key::J}, 1.0);
        REQUIRE(jumpAction->state->phase(*jumpAction) == ActionPhase::Started);
        insys_test::set_keys(manager, keyboard, {}, 1.1);
        REQUIRE(jumpAction->state->phase(*jumpAction) == ActionPhase::Waiting);
    }
}

TEST_CASE("Binding override load reports unknown bindings as warnings") {
    ActionAsset asset = asset::AssetLoader::load_from_json(kOverrideAsset);
    const std::string json = R"({"bindings":[
      {"action": "Player/Missing", "path": "<Keyboard>/x"}
    ]})";
    std::vector<std::string> warnings;
    load_binding_overrides(asset, json, &warnings);
    REQUIRE(warnings.size() == 1);
}
