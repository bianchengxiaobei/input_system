#include <catch2/catch_test_macros.hpp>

#include "action_test_helpers.h"
#include "insys/actions/action_asset.h"
#include "insys/actions/action_state.h"
#include "insys/actions/rebinding.h"
#include "insys/asset/asset_loader.h"

using namespace insys;

namespace {

struct RebindFixture {
    insys_test::TestRuntime runtime;
    Manager manager{runtime};
    Keyboard* keyboard = insys_test::add_keyboard(manager);
    ActionAsset asset{"Player"};
    ActionMap* map = nullptr;
    ActionDef* jump = nullptr;

    RebindFixture() {
        map = &asset.add_map("Player");
        jump = &map->add_action("Jump", ActionType::Button);
        map->add_binding("Jump", "<Keyboard>/space");
        asset.enable(manager);
    }

    void set_key(Key key, double time) {
        runtime.m_now = time;
        insys_test::set_keys(manager, keyboard, {key}, time);
    }

    void release(double time) {
        runtime.m_now = time;
        insys_test::set_keys(manager, keyboard, {}, time);
    }
};

} // namespace

TEST_CASE("Rebinding captures a control and applies the override immediately") {
    RebindFixture fixture;
    int matches = 0;
    int completed = 0;
    std::string path;
    RebindingOperation operation;
    operation.with_action(*fixture.jump)
        .with_target_binding(0)
        .with_magnitude_greater_than(0.5f)
        .on_match([&](Control* control) {
            ++matches;
            path = control->path();
        })
        .on_complete([&] { ++completed; })
        .start();

    REQUIRE(operation.running());
    fixture.set_key(Key::A, 1.0);
    REQUIRE_FALSE(operation.running());
    REQUIRE(operation.matched());
    REQUIRE(matches == 1);
    REQUIRE(completed == 1);
    REQUIRE(path == "Keyboard/a");
    REQUIRE(fixture.map->bindings[0].overridePath == "Keyboard/a");

    fixture.release(1.05);
    fixture.set_key(Key::A, 1.1);
    REQUIRE(fixture.jump->state->phase(*fixture.jump) == ActionPhase::Performed);
}

TEST_CASE("Rebinding honors excluded paths and expected control type") {
    RebindFixture fixture;
    RebindingOperation operation;
    operation.with_action(*fixture.jump)
        .with_target_binding(0)
        .with_expected_control_type("Button")
        .with_controls_excluding("<Keyboard>/w")
        .start();

    fixture.set_key(Key::W, 1.0);
    REQUIRE(operation.running());
    REQUIRE(fixture.map->bindings[0].overridePath.empty());

    fixture.release(1.05);
    fixture.set_key(Key::S, 1.1);
    REQUIRE_FALSE(operation.running());
    REQUIRE(fixture.map->bindings[0].overridePath == "Keyboard/s");
}

TEST_CASE("Rebinding cancellation and timeout stop listening") {
    RebindFixture fixture;
    int cancelled = 0;
    RebindingOperation operation;
    operation.with_action(*fixture.jump)
        .with_target_binding(0)
        .with_timeout(0.1f)
        .on_cancel([&] { ++cancelled; })
        .start();
    REQUIRE(operation.running());

    fixture.runtime.m_now = 1.2;
    fixture.manager.update();
    REQUIRE_FALSE(operation.running());
    REQUIRE(cancelled == 1);

    RebindingOperation explicitCancel;
    explicitCancel.with_action(*fixture.jump).with_target_binding(0).start();
    explicitCancel.cancel();
    REQUIRE_FALSE(explicitCancel.running());
}

TEST_CASE("Rebinding a loaded asset with multiple bindings triggers the new key") {
    insys_test::TestRuntime runtime;
    Manager manager{runtime};
    Keyboard* keyboard = insys_test::add_keyboard(manager);
    ActionAsset asset = asset::AssetLoader::load_from_json(R"({
      "version":1,"maps":[{"name":"Player","actions":[{"name":"Jump"}],
      "bindings":[{"action":"Jump","path":"<Keyboard>/space"},
                  {"action":"Jump","path":"<Gamepad>/buttonSouth"}]}]})");
    asset.enable(manager);
    ActionDef* jump = asset.find_action("Jump");
    RebindingOperation operation;
    operation.with_action(*jump).with_target_binding(0).with_magnitude_greater_than(0.5f).start();
    insys_test::set_keys(manager, keyboard, {Key::J}, 1.0);
    insys_test::set_keys(manager, keyboard, {}, 1.05);
    insys_test::set_keys(manager, keyboard, {Key::J}, 1.1);
    REQUIRE(jump->state->phase(*jump) == ActionPhase::Performed);
}
