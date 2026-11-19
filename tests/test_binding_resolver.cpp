#include <catch2/catch_test_macros.hpp>

#include "action_test_helpers.h"
#include "insys/actions/action_map.h"
#include "insys/actions/binding_resolver.h"
#include "insys/actions/composite.h"

using namespace insys;

TEST_CASE("Resolver maps simple bindings to controls") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Keyboard* keyboard = insys_test::add_keyboard(manager);

    ActionMap map("Player");
    ActionDef& jump = map.add_action("Jump", ActionType::Button);
    map.add_binding("Jump", "<Keyboard>/space");
    map.add_binding("Jump", "<Keyboard>/enter");

    BindingResolver resolver(manager);
    ResolvedBindings resolved = resolver.resolve({&map});

    REQUIRE(resolved.maps.size() == 1);
    REQUIRE(resolved.actions.size() == 1);
    REQUIRE(resolved.controls.size() == 2);
    REQUIRE(resolved.memory.mapCount == 1);
    REQUIRE(resolved.memory.actionCount == 1);
    REQUIRE(resolved.memory.bindingCount == 2);
    REQUIRE(resolved.memory.controlCount == 2);

    REQUIRE(resolved.memory.actionStates[0].phase_value() == ActionPhase::Disabled);
    REQUIRE(resolved.memory.bindingStates[0].actionIndex == 0);
    REQUIRE(resolved.memory.bindingStates[0].controlCount == 1);
    REQUIRE(resolved.memory.bindingStates[1].controlCount == 1);
    REQUIRE(resolved.controls[0] == keyboard->key_control(Key::Space));
    REQUIRE(resolved.controls[1] == keyboard->key_control(Key::Enter));
    REQUIRE(jump.actionIndex == 0);
}

TEST_CASE("Resolver builds composite bindings and parts") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    insys_test::add_keyboard(manager);

    ActionMap map("Player");
    ActionDef& move = map.add_action("Move", ActionType::Value);
    map.add_composite_binding("Move", "2DVector(mode=0)");
    map.add_composite_part("Move", "up", "<Keyboard>/w");
    map.add_composite_part("Move", "down", "<Keyboard>/s");
    map.add_composite_part("Move", "left", "<Keyboard>/a");
    map.add_composite_part("Move", "right", "<Keyboard>/d");

    BindingResolver resolver(manager);
    ResolvedBindings resolved = resolver.resolve({&map});

    REQUIRE(resolved.composites.size() == 1);
    REQUIRE(resolved.memory.compositeCount == 1);
    REQUIRE(resolved.memory.bindingCount == 5);
    REQUIRE(resolved.memory.controlCount == 4);
    REQUIRE(resolved.memory.bindingStates[0].is_composite());
    REQUIRE(resolved.memory.bindingStates[0].controlCount == 4);
    REQUIRE(resolved.memory.bindingStates[0].compositeOrCompositeBindingIndex == 0);
    REQUIRE(resolved.memory.bindingStates[1].is_part_of_composite());
    REQUIRE(resolved.memory.bindingStates[1].compositeOrCompositeBindingIndex == 0);
    REQUIRE(resolved.memory.bindingStates[1].partIndex == 0);
    REQUIRE(resolved.memory.bindingStates[4].partIndex == 3);
    REQUIRE(resolved.composites[0]->part_binding_index(0) == 1);
    REQUIRE(resolved.composites[0]->part_binding_index(3) == 4);
    REQUIRE(move.actionIndex == 0);
}

TEST_CASE("Resolver applies binding and action processors") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    insys_test::add_keyboard(manager);

    ActionMap map("Player");
    ActionDef& move = map.add_action("Move", ActionType::Value);
    move.processors = "scale(factor=2)";
    map.add_binding("Move", "<Keyboard>/w", "W").processors = "invert";

    BindingResolver resolver(manager);
    ResolvedBindings resolved = resolver.resolve({&map});

    REQUIRE(resolved.processors.size() == 2);
    REQUIRE(resolved.memory.bindingStates[0].processorCount == 2);
    REQUIRE(resolved.memory.bindingStates[0].processorStartIndex == 0);
    REQUIRE(resolved.processors[0]->value_type() == ValueType::Float);
    REQUIRE(resolved.processors[1]->value_type() == ValueType::Float);
}

TEST_CASE("Resolver configures modifier composite value type from its binding part") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    insys_test::add_keyboard(manager);
    insys_test::add_gamepad(manager);

    ActionMap map("Player");
    map.add_action("Look", ActionType::Value);
    map.add_composite_binding("Look", "OneModifier");
    map.add_composite_part("Look", "modifier", "<Keyboard>/leftShift");
    map.add_composite_part("Look", "binding", "<Gamepad>/leftStick");

    BindingResolver resolver(manager);
    ResolvedBindings resolved = resolver.resolve({&map});

    REQUIRE(resolved.composites.size() == 1);
    REQUIRE(resolved.composites[0]->value_type() == ValueType::Vector2);
    REQUIRE(resolved.composites[0]->value_size_bytes() == sizeof(Vector2));
}
