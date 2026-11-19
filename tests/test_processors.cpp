#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "action_test_helpers.h"
#include "insys/actions/descriptor_parser.h"
#include "insys/actions/processor.h"

using namespace insys;

namespace {

std::unique_ptr<IProcessor> make(std::string_view text) {
    const auto items = parse_list(text);
    REQUIRE(items.size() == 1);
    return make_processor(items[0].name, items[0].parameters);
}

} // namespace

TEST_CASE("Scalar processors") {
    auto invert = make("Invert");
    float value = 2.0f;
    invert->process(&value, sizeof(value), nullptr);
    REQUIRE(value == -2.0f);

    auto scale = make("Scale(factor=3)");
    value = 2.0f;
    scale->process(&value, sizeof(value), nullptr);
    REQUIRE(value == 6.0f);

    auto clamp = make("Clamp(min=-1,max=1)");
    value = 4.0f;
    clamp->process(&value, sizeof(value), nullptr);
    REQUIRE(value == 1.0f);

    auto normalize = make("Normalize(min=0,max=10,zero=0)");
    value = 5.0f;
    normalize->process(&value, sizeof(value), nullptr);
    REQUIRE(value == Catch::Approx(0.5f));
}

TEST_CASE("Vector processors") {
    auto invert = make("InvertVector2(invertY=false)");
    Vector2 vector{1.0f, 2.0f};
    invert->process(&vector, sizeof(vector), nullptr);
    REQUIRE(vector.x == -1.0f);
    REQUIRE(vector.y == 2.0f);

    auto scale = make("ScaleVector2(x=2,y=3)");
    vector = {2.0f, 2.0f};
    scale->process(&vector, sizeof(vector), nullptr);
    REQUIRE(vector.x == 4.0f);
    REQUIRE(vector.y == 6.0f);

    auto normalizeVector = make("NormalizeVector2");
    vector = {3.0f, 4.0f};
    normalizeVector->process(&vector, sizeof(vector), nullptr);
    REQUIRE(vector.x == Catch::Approx(0.6f));
    REQUIRE(vector.y == Catch::Approx(0.8f));
}

TEST_CASE("Deadzone processors") {
    auto axis = make("AxisDeadzone");
    float value = 0.05f;
    axis->process(&value, sizeof(value), nullptr);
    REQUIRE(value == 0.0f);

    value = 0.5f;
    axis->process(&value, sizeof(value), nullptr);
    REQUIRE(value == Catch::Approx((0.5f - 0.125f) / (0.925f - 0.125f)));

    value = 1.0f;
    axis->process(&value, sizeof(value), nullptr);
    REQUIRE(value == 1.0f);

    auto stick = make("StickDeadzone");
    Vector2 vector{0.5f, 0.0f};
    stick->process(&vector, sizeof(vector), nullptr);
    REQUIRE(vector.x == Catch::Approx((0.5f - 0.125f) / (0.925f - 0.125f)));
    REQUIRE(vector.y == 0.0f);

    vector = {0.0f, 0.0f};
    stick->process(&vector, sizeof(vector), nullptr);
    REQUIRE(vector.x == 0.0f);
    REQUIRE(vector.y == 0.0f);
}

TEST_CASE("Smoothing processors use runtime time") {
    insys_test::TestRuntime runtime;
    Settings settings;
    ProcessorContext context{&settings, &runtime};

    auto smooth = make("Smooth(halfLife=0.1)");
    REQUIRE(smooth->value_type() == ValueType::Float);
    REQUIRE(smooth->caching_policy() == IProcessor::CachingPolicy::EvaluateOnEveryRead);

    float value = 0.0f;
    runtime.m_now = 1.0;
    smooth->process(&value, sizeof(value), nullptr, context);
    REQUIRE(value == 0.0f);

    value = 10.0f;
    runtime.m_now = 1.1;
    smooth->process(&value, sizeof(value), nullptr, context);
    REQUIRE(value == Catch::Approx(5.0f));

    runtime.m_now = 1.1;
    value = 10.0f;
    smooth->process(&value, sizeof(value), nullptr, context);
    REQUIRE(value == Catch::Approx(5.0f));

    value = 0.0f;
    runtime.m_now = 1.2;
    smooth->process(&value, sizeof(value), nullptr, context);
    REQUIRE(value == Catch::Approx(2.5f));

    auto vector2 = make("SmoothVector2(halfLife=0.1)");
    Vector2 two{0.0f, 0.0f};
    runtime.m_now = 2.0;
    vector2->process(&two, sizeof(two), nullptr, context);
    two = {10.0f, 20.0f};
    runtime.m_now = 2.1;
    vector2->process(&two, sizeof(two), nullptr, context);
    REQUIRE(two.x == Catch::Approx(5.0f));
    REQUIRE(two.y == Catch::Approx(10.0f));

    auto vector3 = make("SmoothVector3(halfLife=0.1)");
    Vector3 three{0.0f, 0.0f, 0.0f};
    runtime.m_now = 3.0;
    vector3->process(&three, sizeof(three), nullptr, context);
    three = {10.0f, 20.0f, 30.0f};
    runtime.m_now = 3.1;
    vector3->process(&three, sizeof(three), nullptr, context);
    REQUIRE(three.x == Catch::Approx(5.0f));
    REQUIRE(three.y == Catch::Approx(10.0f));
    REQUIRE(three.z == Catch::Approx(15.0f));

    auto disabled = make("Smooth(halfLife=0)");
    value = 7.0f;
    disabled->process(&value, sizeof(value), nullptr, context);
    REQUIRE(value == 7.0f);
}

TEST_CASE("CompensateDirection rotates by screen orientation") {
    insys_test::TestRuntime runtime;
    Settings settings;
    ProcessorContext context{&settings, &runtime};
    auto processor = make("CompensateDirection");

    REQUIRE(processor->value_type() == ValueType::Vector3);
    REQUIRE(processor->caching_policy() == IProcessor::CachingPolicy::EvaluateOnEveryRead);

    Vector3 direction{1.0f, 0.0f, 0.0f};
    runtime.set_screen_orientation(static_cast<int>(ScreenOrientation::LandscapeLeft));
    processor->process(&direction, sizeof(direction), nullptr, context);
    REQUIRE(direction.x == Catch::Approx(0.0f).margin(1e-6f));
    REQUIRE(direction.y == Catch::Approx(1.0f));

    direction = {1.0f, 0.0f, 0.0f};
    runtime.set_screen_orientation(static_cast<int>(ScreenOrientation::LandscapeRight));
    processor->process(&direction, sizeof(direction), nullptr, context);
    REQUIRE(direction.x == Catch::Approx(0.0f).margin(1e-6f));
    REQUIRE(direction.y == Catch::Approx(-1.0f));

    direction = {1.0f, 0.0f, 0.0f};
    runtime.set_screen_orientation(static_cast<int>(ScreenOrientation::PortraitUpsideDown));
    processor->process(&direction, sizeof(direction), nullptr, context);
    REQUIRE(direction.x == Catch::Approx(-1.0f));
    REQUIRE(direction.y == Catch::Approx(0.0f).margin(1e-6f));

    settings.compensateForScreenOrientation = false;
    direction = {1.0f, 0.0f, 0.0f};
    runtime.set_screen_orientation(static_cast<int>(ScreenOrientation::LandscapeLeft));
    processor->process(&direction, sizeof(direction), nullptr, context);
    REQUIRE(direction.x == 1.0f);
    REQUIRE(direction.y == 0.0f);
}

TEST_CASE("CompensateRotation applies orientation quaternion") {
    insys_test::TestRuntime runtime;
    Settings settings;
    ProcessorContext context{&settings, &runtime};
    auto processor = make("CompensateRotation");

    REQUIRE(processor->value_type() == ValueType::Quaternion);
    REQUIRE(processor->caching_policy() == IProcessor::CachingPolicy::EvaluateOnEveryRead);

    Quaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};
    runtime.set_screen_orientation(static_cast<int>(ScreenOrientation::PortraitUpsideDown));
    processor->process(&rotation, sizeof(rotation), nullptr, context);
    REQUIRE(rotation.x == Catch::Approx(0.0f).margin(1e-6f));
    REQUIRE(rotation.y == Catch::Approx(0.0f).margin(1e-6f));
    REQUIRE(rotation.z == Catch::Approx(1.0f));
    REQUIRE(rotation.w == Catch::Approx(0.0f).margin(1e-6f));

    rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    runtime.set_screen_orientation(static_cast<int>(ScreenOrientation::LandscapeLeft));
    processor->process(&rotation, sizeof(rotation), nullptr, context);
    REQUIRE(rotation.z == Catch::Approx(0.70710678f));
    REQUIRE(rotation.w == Catch::Approx(-0.70710678f));

    rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    runtime.set_screen_orientation(static_cast<int>(ScreenOrientation::LandscapeRight));
    processor->process(&rotation, sizeof(rotation), nullptr, context);
    REQUIRE(rotation.z == Catch::Approx(-0.70710678f));
    REQUIRE(rotation.w == Catch::Approx(-0.70710678f));
}

TEST_CASE("AndroidCompensateDirection scales gravity and compensates orientation") {
    insys_test::TestRuntime runtime;
    Settings settings;
    ProcessorContext context{&settings, &runtime};
    auto processor = make("AndroidCompensateDirection");

    REQUIRE(processor->value_type() == ValueType::Vector3);
    REQUIRE(processor->caching_policy() == IProcessor::CachingPolicy::EvaluateOnEveryRead);

    Vector3 direction{0.0f, 9.80665f, 0.0f};
    processor->process(&direction, sizeof(direction), nullptr, context);
    REQUIRE(direction.x == Catch::Approx(0.0f).margin(1e-6f));
    REQUIRE(direction.y == Catch::Approx(-1.0f));
    REQUIRE(direction.z == Catch::Approx(0.0f).margin(1e-6f));

    direction = {0.0f, 9.80665f, 0.0f};
    runtime.set_screen_orientation(static_cast<int>(ScreenOrientation::LandscapeLeft));
    processor->process(&direction, sizeof(direction), nullptr, context);
    REQUIRE(direction.x == Catch::Approx(1.0f));
    REQUIRE(direction.y == Catch::Approx(0.0f).margin(1e-6f));

    settings.compensateForScreenOrientation = false;
    direction = {0.0f, 9.80665f, 0.0f};
    processor->process(&direction, sizeof(direction), nullptr, context);
    REQUIRE(direction.x == Catch::Approx(0.0f).margin(1e-6f));
    REQUIRE(direction.y == Catch::Approx(-1.0f));
}

TEST_CASE("AndroidCompensateRotation reconstructs quaternion and compensates orientation") {
    insys_test::TestRuntime runtime;
    Settings settings;
    ProcessorContext context{&settings, &runtime};
    auto processor = make("AndroidCompensateRotation");

    REQUIRE(processor->value_type() == ValueType::Quaternion);
    REQUIRE(processor->caching_policy() == IProcessor::CachingPolicy::EvaluateOnEveryRead);

    Quaternion rotation{0.0f, 0.0f, 0.6f, 0.0f};
    processor->process(&rotation, sizeof(rotation), nullptr, context);
    REQUIRE(rotation.z == Catch::Approx(0.6f));
    REQUIRE(rotation.w == Catch::Approx(0.8f));

    rotation = {0.0f, 0.0f, 0.6f, 0.0f};
    runtime.set_screen_orientation(static_cast<int>(ScreenOrientation::PortraitUpsideDown));
    processor->process(&rotation, sizeof(rotation), nullptr, context);
    REQUIRE(rotation.z == Catch::Approx(0.8f));
    REQUIRE(rotation.w == Catch::Approx(-0.6f));

    rotation = {1.0f, 1.0f, 1.0f, 0.0f};
    runtime.set_screen_orientation(static_cast<int>(ScreenOrientation::Unknown));
    processor->process(&rotation, sizeof(rotation), nullptr, context);
    REQUIRE(rotation.w == 0.0f);
}

TEST_CASE("Deadzone curve remaps normalized axis and stick input") {
    auto axis = make("AxisDeadzoneCurve(min=0.1,max=0.9,curve=\"0,0;0.5,0.25;1,1\")");
    float value = 0.5f;
    axis->process(&value, sizeof(value), nullptr);
    REQUIRE(value == Catch::Approx(0.25f));

    value = -0.5f;
    axis->process(&value, sizeof(value), nullptr);
    REQUIRE(value == Catch::Approx(-0.25f));

    value = 0.3f;
    axis->process(&value, sizeof(value), nullptr);
    REQUIRE(value == Catch::Approx(0.125f));

    auto identity = make("AxisDeadzone(min=0.1,max=0.9)");
    value = 0.5f;
    identity->process(&value, sizeof(value), nullptr);
    REQUIRE(value == Catch::Approx(0.5f));

    auto invalid = make("AxisDeadzoneCurve(min=0.1,max=0.9,curve=\"0,0;0.5,2;1,1\")");
    value = 0.5f;
    invalid->process(&value, sizeof(value), nullptr);
    REQUIRE(value == Catch::Approx(0.5f));

    auto stick = make("StickDeadzoneCurve(min=0.1,max=0.9,curve=\"0,0;0.5,0.25;1,1\")");
    Vector2 vector{0.5f, 0.0f};
    stick->process(&vector, sizeof(vector), nullptr);
    REQUIRE(vector.x == Catch::Approx(0.25f));
    REQUIRE(vector.y == 0.0f);

    vector = {0.0f, 0.5f};
    stick->process(&vector, sizeof(vector), nullptr);
    REQUIRE(vector.x == 0.0f);
    REQUIRE(vector.y == Catch::Approx(0.25f));
}
