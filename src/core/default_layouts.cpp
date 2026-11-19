#include "insys/layout_registry.h"

#include "insys/device.h"
#include <cstddef>

#include "insys/device_states.h"
#include "insys/hid/hid_gamepad.h"
#include "insys/low/key.h"
#include "insys/low/state_format.h"

namespace insys {
namespace {

ControlItem make_key_item(Key key) {
    ControlItem item;
    item.name = key_name(key);
    item.layout = "Key";
    item.displayName = key_layout_name(key);
    item.format = kFormatBit;
    item.sizeInBits = 1;
    item.bit = static_cast<uint32_t>(key);
    item.keyCode = static_cast<int>(key);

    if (key == Key::Space || key == Key::Enter)
        item.usages.push_back("Submit");
    if (key == Key::Escape)
        item.usages.push_back("Cancel");
    if (key == Key::LeftShift || key == Key::RightShift ||
        key == Key::LeftCtrl || key == Key::RightCtrl ||
        key == Key::LeftAlt || key == Key::RightAlt)
        item.usages.push_back("Modifier");
    return item;
}

ControlItem make_bit_button(std::string name, uint32_t byteOffset, uint32_t bit,
                            std::string layout = "Button") {
    ControlItem item;
    item.name = std::move(name);
    item.layout = std::move(layout);
    item.format = kFormatBit;
    item.sizeInBits = 1;
    item.offset = byteOffset;
    item.bit = bit;
    return item;
}

ControlItem make_float_control(std::string name, std::string layout, uint32_t byteOffset) {
    ControlItem item;
    item.name = std::move(name);
    item.layout = std::move(layout);
    item.format = kFormatFloat;
    item.sizeInBits = 32;
    item.offset = byteOffset;
    return item;
}

ControlItem make_integer_control(std::string name, uint32_t byteOffset,
                                 uint32_t sizeInBits = 32) {
    ControlItem item;
    item.name = std::move(name);
    item.layout = "Integer";
    item.format = kFormatInt;
    item.sizeInBits = sizeInBits;
    item.offset = byteOffset;
    return item;
}

ControlItem make_vector2_control(std::string name, std::string layout, uint32_t byteOffset) {
    ControlItem item;
    item.name = std::move(name);
    item.layout = std::move(layout);
    item.format = kFormatVector2;
    item.sizeInBits = 64;
    item.offset = byteOffset;
    return item;
}

ControlItem make_vector3_control(std::string name, std::string layout, uint32_t byteOffset) {
    ControlItem item;
    item.name = std::move(name);
    item.layout = std::move(layout);
    item.format = kFormatVector3;
    item.sizeInBits = 96;
    item.offset = byteOffset;
    return item;
}

ControlItem make_quaternion_control(std::string name, std::string layout, uint32_t byteOffset) {
    ControlItem item;
    item.name = std::move(name);
    item.layout = std::move(layout);
    item.format = kFormatQuaternion;
    item.sizeInBits = 128;
    item.offset = byteOffset;
    return item;
}

std::unique_ptr<Layout> make_keyboard_layout() {
    auto layout = std::make_unique<Layout>();
    layout->name = "Keyboard";
    layout->stateFormat = kFormatKeyboard;
    layout->displayName = "Keyboard";
    layout->factory = [] { return std::make_unique<Keyboard>(); };

    for (std::size_t i = 0; i < kKeyCount; ++i)
        layout->controls.push_back(make_key_item(static_cast<Key>(i)));

    ControlItem anyKey;
    anyKey.name = "anyKey";
    anyKey.layout = "AnyKey";
    anyKey.format = kFormatBit;
    anyKey.sizeInBits = 1;
    anyKey.bit = static_cast<uint32_t>(kKeyCount);
    anyKey.isSynthetic = true;
    layout->controls.push_back(anyKey);

    return layout;
}

std::unique_ptr<Layout> make_mouse_layout() {
    auto layout = std::make_unique<Layout>();
    layout->name = "Mouse";
    layout->stateFormat = kFormatMouse;
    layout->stateSizeInBits = 28u * 8u;
    layout->displayName = "Mouse";
    layout->factory = [] { return std::make_unique<Mouse>(); };

    ControlItem position = make_vector2_control("position", "Vector2", 0);
    position.usages.push_back("Point");
    position.dontReset = true;
    layout->controls.push_back(position);

    ControlItem delta = make_vector2_control("delta", "Delta", 8);
    delta.usages.push_back("Secondary2DMotion");
    layout->controls.push_back(delta);

    ControlItem scroll = make_vector2_control("scroll", "Vector2", 16);
    scroll.usages.push_back("Scroll");
    layout->controls.push_back(scroll);

    ControlItem left = make_bit_button("leftButton", 24, 0);
    left.usages.push_back("PrimaryAction");
    left.usages.push_back("Submit");
    layout->controls.push_back(left);

    ControlItem right = make_bit_button("rightButton", 24, 1);
    right.usages.push_back("SecondaryAction");
    right.usages.push_back("Cancel");
    layout->controls.push_back(right);

    ControlItem middle = make_bit_button("middleButton", 24, 2);
    middle.usages.push_back("TertiaryAction");
    layout->controls.push_back(middle);

    layout->controls.push_back(make_bit_button("forwardButton", 24, 3));
    layout->controls.push_back(make_bit_button("backButton", 24, 4));

    return layout;
}

std::unique_ptr<Layout> make_gamepad_layout() {
    auto layout = std::make_unique<Layout>();
    layout->name = "Gamepad";
    layout->stateFormat = kFormatGamepad;
    layout->displayName = "Gamepad";
    layout->factory = [] { return std::make_unique<Gamepad>(); };

    struct ButtonSpec {
        const char* name;
        uint32_t bit;
        const char* usage;
        const char* alias1;
        const char* alias2;
    };

    const ButtonSpec buttons[] = {
        {"dpadUp", 0, "Navigate", nullptr, nullptr},
        {"dpadDown", 1, "Navigate", nullptr, nullptr},
        {"dpadLeft", 2, "Navigate", nullptr, nullptr},
        {"dpadRight", 3, "Navigate", nullptr, nullptr},
        {"buttonNorth", 4, nullptr, "y", "triangle"},
        {"buttonEast", 5, "Cancel", "b", "circle"},
        {"buttonSouth", 6, "PrimaryAction", "a", "cross"},
        {"buttonWest", 7, "SecondaryAction", "x", "square"},
        {"leftStickButton", 8, nullptr, "l3", nullptr},
        {"rightStickButton", 9, nullptr, "r3", nullptr},
        {"leftShoulder", 10, nullptr, "l1", "lb"},
        {"rightShoulder", 11, nullptr, "r1", "rb"},
        {"start", 12, "Submit", "menu", "options"},
        {"select", 13, nullptr, "view", "share"},
    };

    for (const ButtonSpec& spec : buttons) {
        ControlItem item = make_bit_button(spec.name, 0, spec.bit);
        if (spec.usage != nullptr)
            item.usages.push_back(spec.usage);
        if (spec.alias1 != nullptr)
            item.aliases.push_back(spec.alias1);
        if (spec.alias2 != nullptr)
            item.aliases.push_back(spec.alias2);
        layout->controls.push_back(item);
    }

    if (ControlItem* south = &layout->controls[6])
        south->aliases.push_back("submit");

    ControlItem dpad;
    dpad.name = "dpad";
    dpad.layout = "Dpad";
    dpad.format = kFormatBit;
    dpad.sizeInBits = 32;
    dpad.offset = 0;
    dpad.bit = 0;
    dpad.usages.push_back("Hat");
    layout->controls.push_back(dpad);

    ControlItem leftStick = make_vector2_control("leftStick", "Stick", 4);
    leftStick.usages.push_back("Stick");
    layout->controls.push_back(leftStick);

    ControlItem rightStick = make_vector2_control("rightStick", "Stick", 12);
    rightStick.usages.push_back("Stick");
    layout->controls.push_back(rightStick);

    ControlItem leftTrigger = make_float_control("leftTrigger", "Button", 20);
    leftTrigger.usages.push_back("Trigger");
    layout->controls.push_back(leftTrigger);

    ControlItem rightTrigger = make_float_control("rightTrigger", "Button", 24);
    rightTrigger.usages.push_back("Trigger");
    layout->controls.push_back(rightTrigger);

    return layout;
}

std::unique_ptr<Layout> make_hid_gamepad_layout() {
    auto layout = std::make_unique<Layout>();
    layout->name = "HidGamepad";
    layout->extends = "Gamepad";
    layout->stateFormat = kFormatHidGamepad;
    layout->stateSizeInBits = sizeof(hid::HidGamepadState) * 8;
    layout->displayName = "HID Gamepad";
    layout->factory = [] { return std::make_unique<Gamepad>(); };

    const uint32_t extraOffset = static_cast<uint32_t>(offsetof(hid::HidGamepadState, extraButtons));
    layout->controls.push_back(make_bit_button("leftTriggerButton", extraOffset, 0));
    layout->controls.push_back(make_bit_button("rightTriggerButton", extraOffset, 1));
    layout->controls.push_back(make_bit_button("systemButton", extraOffset, 2));
    layout->controls.push_back(make_bit_button("touchpadButton", extraOffset, 3));
    layout->controls.push_back(make_bit_button("micButton", extraOffset, 4));
    layout->controls.push_back(make_bit_button("capture", extraOffset, 5));
    layout->controls.push_back(make_bit_button("home", extraOffset, 6));
    layout->controls.push_back(make_bit_button("leftGrip", extraOffset, 7));
    layout->controls.push_back(make_bit_button("rightGrip", extraOffset, 8));
    layout->controls.push_back(make_bit_button("leftBack", extraOffset, 9));
    layout->controls.push_back(make_bit_button("rightBack", extraOffset, 10));
    layout->controls.push_back(make_bit_button("touchpadContact1", extraOffset, 11));
    layout->controls.push_back(make_bit_button("touchpadContact2", extraOffset, 12));

    layout->controls.push_back(make_vector2_control(
        "leftTouchpad", "Vector2", static_cast<uint32_t>(offsetof(hid::HidGamepadState, leftTouchpad))));
    layout->controls.push_back(make_vector2_control(
        "rightTouchpad", "Vector2", static_cast<uint32_t>(offsetof(hid::HidGamepadState, rightTouchpad))));
    layout->controls.push_back(make_vector3_control(
        "angularVelocity", "Vector3",
        static_cast<uint32_t>(offsetof(hid::HidGamepadState, angularVelocity))));
    layout->controls.push_back(make_vector3_control(
        "acceleration", "Vector3",
        static_cast<uint32_t>(offsetof(hid::HidGamepadState, acceleration))));
    return layout;
}

std::unique_ptr<Layout> make_product_layout(std::string name, std::string displayName) {
    auto layout = std::make_unique<Layout>();
    layout->name = std::move(name);
    layout->displayName = std::move(displayName);
    layout->extends = "HidGamepad";
    return layout;
}

std::unique_ptr<Layout> make_generic_hid_layout() {
    auto layout = std::make_unique<Layout>();
    layout->name = "HID";
    layout->displayName = "HID";
    layout->stateFormat = kFormatHid;
    layout->factory = [] { return std::make_unique<Device>(); };
    return layout;
}

std::unique_ptr<Layout> make_joystick_layout() {
    auto layout = std::make_unique<Layout>();
    layout->name = "Joystick";
    layout->stateFormat = kFormatJoystick;
    layout->displayName = "Joystick";
    layout->factory = [] { return std::make_unique<Joystick>(); };

    for (uint32_t bit = 0; bit < 20; ++bit) {
        const std::string name = "button" + std::to_string(bit);
        layout->controls.push_back(make_bit_button(name, 0, bit));
    }

    ControlItem stick = make_vector2_control("stick", "Stick", 4);
    stick.usages.push_back("Stick");
    layout->controls.push_back(stick);

    layout->controls.push_back(make_float_control("trigger", "Axis", 12));

    return layout;
}

std::unique_ptr<Layout> make_sensor_layout(std::string name, FourCC format,
                                           std::string controlName,
                                           std::string controlLayout,
                                           uint32_t stateSizeInBits,
                                           std::function<std::unique_ptr<Control>()> factory) {
    auto layout = std::make_unique<Layout>();
    layout->name = std::move(name);
    layout->stateFormat = format;
    layout->stateSizeInBits = stateSizeInBits;
    layout->displayName = layout->name;
    layout->factory = std::move(factory);

    ControlItem item;
    item.name = std::move(controlName);
    item.layout = std::move(controlLayout);
    item.format = format == kFormatAttitude ? kFormatQuaternion : kFormatVector3;
    item.sizeInBits = stateSizeInBits;
    item.offset = 0;
    layout->controls.push_back(std::move(item));
    return layout;
}

std::unique_ptr<Layout> make_scalar_sensor_layout(std::string name, FourCC format,
                                                std::string controlName,
                                                uint32_t stateSizeInBits,
                                                std::function<std::unique_ptr<Control>()> factory) {
    auto layout = std::make_unique<Layout>();
    layout->name = std::move(name);
    layout->stateFormat = format;
    layout->stateSizeInBits = stateSizeInBits;
    layout->displayName = layout->name;
    layout->factory = std::move(factory);
    layout->controls.push_back(
        make_float_control(std::move(controlName), "Axis", 0));
    return layout;
}

std::unique_ptr<Layout> make_gyroscope_layout() {
    return make_sensor_layout("Gyroscope", kFormatGyroscope, "angularVelocity", "Vector3",
                              sizeof(GyroscopeState) * 8,
                              [] { return std::make_unique<Gyroscope>(); });
}

std::unique_ptr<Layout> make_accelerometer_layout() {
    return make_sensor_layout("Accelerometer", kFormatAccelerometer, "acceleration",
                              "Vector3", sizeof(AccelerometerState) * 8,
                              [] { return std::make_unique<Accelerometer>(); });
}

std::unique_ptr<Layout> make_gravity_layout() {
    return make_sensor_layout("Gravity", kFormatGravity, "gravity", "Vector3",
                              sizeof(GravityState) * 8,
                              [] { return std::make_unique<Gravity>(); });
}

std::unique_ptr<Layout> make_linear_acceleration_layout() {
    return make_sensor_layout("LinearAcceleration", kFormatLinearAcceleration,
                              "acceleration", "Vector3",
                              sizeof(LinearAccelerationState) * 8,
                              [] { return std::make_unique<LinearAcceleration>(); });
}

std::unique_ptr<Layout> make_magnetic_field_layout() {
    return make_sensor_layout("MagneticField", kFormatMagneticField,
                              "magneticField", "Vector3",
                              sizeof(MagneticFieldState) * 8,
                              [] { return std::make_unique<MagneticField>(); });
}

std::unique_ptr<Layout> make_attitude_layout() {
    return make_sensor_layout("Attitude", kFormatAttitude, "attitude", "Quaternion",
                              sizeof(AttitudeState) * 8,
                              [] { return std::make_unique<Attitude>(); });
}

std::unique_ptr<Layout> make_light_layout() {
    return make_scalar_sensor_layout("Light", kFormatLight, "lightLevel",
                                     sizeof(LightSensorState) * 8,
                                     [] { return std::make_unique<LightSensor>(); });
}

std::unique_ptr<Layout> make_pressure_layout() {
    return make_scalar_sensor_layout(
        "Pressure", kFormatPressure, "atmosphericPressure",
        sizeof(PressureSensorState) * 8,
        [] { return std::make_unique<PressureSensor>(); });
}

std::unique_ptr<Layout> make_proximity_layout() {
    return make_scalar_sensor_layout(
        "Proximity", kFormatProximity, "distance",
        sizeof(ProximitySensorState) * 8,
        [] { return std::make_unique<ProximitySensor>(); });
}

std::unique_ptr<Layout> make_humidity_layout() {
    return make_scalar_sensor_layout(
        "Humidity", kFormatHumidity, "relativeHumidity",
        sizeof(HumiditySensorState) * 8,
        [] { return std::make_unique<HumiditySensor>(); });
}

std::unique_ptr<Layout> make_ambient_temperature_layout() {
    return make_scalar_sensor_layout(
        "AmbientTemperature", kFormatAmbientTemperature, "ambientTemperature",
        sizeof(AmbientTemperatureSensorState) * 8,
        [] { return std::make_unique<AmbientTemperatureSensor>(); });
}

std::unique_ptr<Layout> make_step_counter_layout() {
    auto layout = std::make_unique<Layout>();
    layout->name = "StepCounter";
    layout->stateFormat = kFormatStepCounter;
    layout->stateSizeInBits = sizeof(StepCounterState) * 8;
    layout->displayName = "Step Counter";
    layout->factory = [] { return std::make_unique<StepCounter>(); };
    layout->controls.push_back(make_integer_control("stepCounter", 0));
    return layout;
}

std::unique_ptr<Layout> make_hinge_angle_layout() {
    return make_scalar_sensor_layout("HingeAngle", kFormatHingeAngle, "angle",
                                     sizeof(HingeAngleState) * 8,
                                     [] { return std::make_unique<HingeAngle>(); });
}

std::unique_ptr<Layout> make_pen_layout() {
    auto layout = std::make_unique<Layout>();
    layout->name = "Pen";
    layout->stateFormat = kFormatPen;
    layout->stateSizeInBits = sizeof(PenState) * 8;
    layout->displayName = "Pen";
    layout->factory = [] { return std::make_unique<Pen>(); };

    ControlItem position = make_vector2_control("position", "Vector2", 0);
    position.usages.push_back("Point");
    position.dontReset = true;
    layout->controls.push_back(position);

    ControlItem delta = make_vector2_control("delta", "Delta", 8);
    delta.usages.push_back("Secondary2DMotion");
    layout->controls.push_back(delta);

    ControlItem tilt = make_vector2_control("tilt", "Vector2", 16);
    tilt.usages.push_back("Tilt");
    layout->controls.push_back(tilt);

    ControlItem pressure = make_float_control("pressure", "Axis", 24);
    pressure.usages.push_back("Pressure");
    layout->controls.push_back(pressure);

    ControlItem twist = make_float_control("twist", "Axis", 28);
    twist.usages.push_back("Twist");
    layout->controls.push_back(twist);

    ControlItem tip = make_bit_button("tip", 32, 0);
    tip.usages.push_back("PrimaryAction");
    layout->controls.push_back(tip);
    layout->controls.push_back(make_bit_button("eraser", 32, 1));

    ControlItem barrel1 = make_bit_button("barrel1", 32, 2);
    barrel1.usages.push_back("SecondaryAction");
    barrel1.aliases.push_back("barrelFirst");
    layout->controls.push_back(barrel1);

    ControlItem barrel2 = make_bit_button("barrel2", 32, 3);
    barrel2.aliases.push_back("barrelSecond");
    layout->controls.push_back(barrel2);
    layout->controls.push_back(make_bit_button("inRange", 32, 4));

    ControlItem barrel3 = make_bit_button("barrel3", 32, 5);
    barrel3.aliases.push_back("barrelThird");
    layout->controls.push_back(barrel3);

    ControlItem barrel4 = make_bit_button("barrel4", 32, 6);
    barrel4.aliases.push_back("barrelFourth");
    layout->controls.push_back(barrel4);

    layout->controls.push_back(make_integer_control("displayIndex", 34, 16));
    return layout;
}

std::unique_ptr<Layout> make_tracked_device_layout() {
    auto layout = std::make_unique<Layout>();
    layout->name = "TrackedDevice";
    layout->stateFormat = kFormatTrackedDevice;
    layout->stateSizeInBits = sizeof(TrackedDeviceState) * 8;
    layout->displayName = "Tracked Device";
    layout->factory = [] { return std::make_unique<TrackedDevice>(); };

    layout->controls.push_back(make_integer_control("trackingState", 0));
    layout->controls.push_back(make_bit_button("isTracked", 4, 0));
    layout->controls.push_back(
        make_vector3_control("devicePosition", "Vector3", 8));
    layout->controls.push_back(
        make_quaternion_control("deviceRotation", "Quaternion", 20));
    return layout;
}

std::unique_ptr<Layout> make_touchscreen_layout() {
    auto layout = std::make_unique<Layout>();
    layout->name = "Touchscreen";
    layout->stateFormat = kFormatTouchscreen;
    layout->stateSizeInBits = sizeof(TouchscreenState) * 8;
    layout->displayName = "Touchscreen";
    layout->factory = [] { return std::make_unique<Touchscreen>(); };

    for (int touch = 0; touch < kMaxTouches; ++touch) {
        const std::string prefix = "touch" + std::to_string(touch);
        const uint32_t base = static_cast<uint32_t>(touch) * sizeof(TouchPointState);

        ControlItem group;
        group.name = prefix;
        group.layout = "Touch";
        group.format = kFormatTouchscreen;
        group.sizeInBits = sizeof(TouchPointState) * 8;
        group.offset = base;
        layout->controls.push_back(std::move(group));

        layout->controls.push_back(
            make_vector2_control(prefix + "/position", "Vector2", base));
        layout->controls.push_back(
            make_vector2_control(prefix + "/delta", "Vector2", base + 8));
        layout->controls.push_back(
            make_vector2_control(prefix + "/startPosition", "Vector2", base + 16));
        layout->controls.push_back(
            make_float_control(prefix + "/phase", "Axis", base + 24));
        layout->controls.push_back(
            make_float_control(prefix + "/tapCount", "Axis", base + 28));
        layout->controls.push_back(
            make_float_control(prefix + "/press", "Button", base + 32));
    }
    return layout;
}

} // namespace

void register_default_layouts(LayoutRegistry& registry) {
    registry.add(make_keyboard_layout());
    registry.add(make_mouse_layout());
    registry.add(make_gamepad_layout());
    registry.add(make_hid_gamepad_layout());
    registry.add(make_generic_hid_layout());
    registry.add(make_product_layout("DualShock3", "DualShock 3"));
    registry.add(make_product_layout("DualShock3GamepadHID", "DualShock 3 HID"));
    registry.add(make_product_layout("DualShock4", "DualShock 4"));
    registry.add(make_product_layout("DualShock4GamepadHID", "DualShock 4 HID"));
    registry.add(make_product_layout("DualSense", "DualSense"));
    registry.add(make_product_layout("DualSenseGamepadHID", "DualSense HID"));
    registry.add(make_product_layout("SwitchProController", "Switch Pro Controller"));
    registry.add(make_product_layout("SwitchProControllerHID", "Switch Pro Controller HID"));
    registry.add(make_product_layout("SteamController", "Steam Controller"));
    registry.add(make_product_layout("SteamDeckController", "Steam Deck Controller"));
    registry.add(make_product_layout("SteamDeckControllerHID", "Steam Deck Controller HID"));
    registry.add(make_joystick_layout());
    registry.add(make_gyroscope_layout());
    registry.add(make_accelerometer_layout());
    registry.add(make_gravity_layout());
    registry.add(make_linear_acceleration_layout());
    registry.add(make_magnetic_field_layout());
    registry.add(make_attitude_layout());
    registry.add(make_light_layout());
    registry.add(make_pressure_layout());
    registry.add(make_proximity_layout());
    registry.add(make_humidity_layout());
    registry.add(make_ambient_temperature_layout());
    registry.add(make_step_counter_layout());
    registry.add(make_hinge_angle_layout());
    registry.add(make_pen_layout());
    registry.add(make_tracked_device_layout());
    registry.add(make_touchscreen_layout());

    DeviceMatcher keyboardMatcher;
    keyboardMatcher.add("interface", "Keyboard*");
    registry.add_matcher("Keyboard", keyboardMatcher);

    DeviceMatcher mouseMatcher;
    mouseMatcher.add("interface", "Mouse*");
    registry.add_matcher("Mouse", mouseMatcher);

    DeviceMatcher dualSenseMatcher;
    dualSenseMatcher.add("product", ".*DualSense.*", true);
    registry.add_matcher("DualSenseGamepadHID", dualSenseMatcher);

    DeviceMatcher dualShock3Matcher;
    dualShock3Matcher.add(
        "product", ".*(DualShock 3|PLAYSTATION\\(R\\)3 Controller).*", true);
    registry.add_matcher("DualShock3GamepadHID", dualShock3Matcher);

    DeviceMatcher dualShockMatcher;
    dualShockMatcher.add("product", ".*(DualShock|Wireless Controller).*", true);
    registry.add_matcher("DualShock4GamepadHID", dualShockMatcher);

    DeviceMatcher switchMatcher;
    switchMatcher.add("product", ".*Switch Pro.*", true);
    registry.add_matcher("SwitchProControllerHID", switchMatcher);

    DeviceMatcher steamMatcher;
    steamMatcher.add("product", ".*Steam Controller.*", true);
    registry.add_matcher("SteamController", steamMatcher);
    DeviceMatcher steamDeckMatcher;
    steamDeckMatcher.add("product", ".*Steam Deck.*", true);
    registry.add_matcher("SteamDeckControllerHID", steamDeckMatcher);

    DeviceMatcher gamepadMatcher;
    gamepadMatcher.add("product", ".*gamepad.*", true);
    registry.add_matcher("Gamepad", gamepadMatcher);
}

LayoutRegistry& default_layout_registry() {
    static LayoutRegistry* instance = [] {
        auto* registry = new LayoutRegistry();
        register_default_layouts(*registry);
        return registry;
    }();
    return *instance;
}

} // namespace insys








