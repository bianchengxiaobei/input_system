#include "insys/device.h"

#include <cstring>
#include <string>

#include "insys/control_path.h"
#include "insys/device_builder.h"
#include "insys/layout_registry.h"
#include "insys/string_utils.h"

namespace insys {
namespace {

void collect_controls(Control* control, Device* device, std::vector<Control*>& out) {
    control->device = device;
    out.push_back(control);
    control->childStart = static_cast<uint32_t>(out.size());
    control->childCount = static_cast<uint32_t>(control->children.size());
    for (Control* child : control->children)
        collect_controls(child, device, out);
}

} // namespace

uint32_t Device::state_size_bytes() const {
    return stateBlock.aligned_size_in_bytes();
}

void Device::read_value_from_state(const uint8_t* state, void* out) const {
    std::memcpy(out, state, state_size_bytes());
}

void Device::write_value_to_state(uint8_t* state, const void* in) const {
    std::memcpy(state, in, state_size_bytes());
}

void Device::finalize_hierarchy() {
    allControls.clear();
    childrenFlat.clear();
    collect_controls(this, this, allControls);
    for (Control* control : allControls) {
        if (control != this)
            childrenFlat.push_back(control);
    }
}

Control* Device::find_control(std::string_view controlName) const {
    if (controlName.find('/') != std::string_view::npos) {
        std::string fullPath;
        if (controlName.size() > name.str().size() &&
            controlName.substr(0, name.str().size()) == name.str() &&
            controlName[name.str().size()] == '/') {
            fullPath = std::string(controlName);
        } else {
            fullPath = name.str() + "/" + std::string(controlName);
        }
        return insys::find_control(*const_cast<Device*>(this), fullPath);
    }
    for (Control* control : allControls) {
        if (control != this && equals_ignore_case(control->name.str(), controlName))
            return control;
    }
    return nullptr;
}

Control* Device::find_control_by_usage(InternedString usage) const {
    for (Control* control : allControls) {
        for (InternedString candidate : control->usages) {
            if (candidate == usage)
                return control;
        }
    }
    return nullptr;
}

void Keyboard::rebuild_key_cache() {
    m_keyCache.assign(kKeyCount, nullptr);
    for (Control* control : allControls) {
        auto* keyControl = dynamic_cast<KeyControl*>(control);
        if (keyControl == nullptr)
            continue;
        const auto index = static_cast<std::size_t>(keyControl->key);
        if (index < m_keyCache.size())
            m_keyCache[index] = keyControl;
    }
}

Control* Keyboard::key_control(Key key) const {
    const auto index = static_cast<std::size_t>(key);
    if (index < m_keyCache.size())
        return m_keyCache[index];

    for (Control* control : allControls) {
        auto* keyControl = dynamic_cast<KeyControl*>(control);
        if (keyControl != nullptr && keyControl->key == key)
            return keyControl;
    }
    return nullptr;
}

Vector2Control* Mouse::position() const {
    return static_cast<Vector2Control*>(find_control("position"));
}

Vector2Control* Mouse::delta() const {
    return static_cast<Vector2Control*>(find_control("delta"));
}

Vector2Control* Mouse::scroll() const {
    return static_cast<Vector2Control*>(find_control("scroll"));
}

Control* Pen::barrel_button(int index) const {
    if (index < 1 || index > 4)
        return nullptr;
    return find_control("barrel" + std::to_string(index));
}

Control* Touchscreen::touch(int index) const {
    if (index < 0 || index >= touch_count())
        return nullptr;
    return find_control("touch" + std::to_string(index));
}

std::unique_ptr<Device> make_keyboard(int deviceId) {
    DeviceDescription description;
    description.interfaceName = "Keyboard";
    description.deviceClass = "Keyboard";
    description.product = "Keyboard";

    DeviceBuilder builder(default_layout_registry());
    return builder.build("Keyboard", "", deviceId, description);
}

std::unique_ptr<Device> make_mouse(int deviceId) {
    DeviceDescription description;
    description.interfaceName = "Mouse";
    description.deviceClass = "Mouse";
    description.product = "Mouse";

    DeviceBuilder builder(default_layout_registry());
    return builder.build("Mouse", "", deviceId, description);
}

namespace {

std::unique_ptr<Device> make_sensor_device(std::string_view layout, int deviceId,
                                           std::string_view interfaceName,
                                           std::string_view product) {
    DeviceDescription description;
    description.interfaceName = std::string(interfaceName);
    description.deviceClass = std::string(layout);
    description.product = std::string(product);

    DeviceBuilder builder(default_layout_registry());
    return builder.build(layout, "", deviceId, description);
}

} // namespace

std::unique_ptr<Device> make_gyroscope(int deviceId) {
    return make_sensor_device("Gyroscope", deviceId, "Gyroscope", "Gyroscope");
}

std::unique_ptr<Device> make_accelerometer(int deviceId) {
    return make_sensor_device("Accelerometer", deviceId, "Accelerometer",
                              "Accelerometer");
}

std::unique_ptr<Device> make_gravity(int deviceId) {
    return make_sensor_device("Gravity", deviceId, "Gravity", "Gravity");
}

std::unique_ptr<Device> make_linear_acceleration(int deviceId) {
    return make_sensor_device("LinearAcceleration", deviceId,
                              "LinearAcceleration", "Linear Acceleration");
}

std::unique_ptr<Device> make_magnetic_field(int deviceId) {
    return make_sensor_device("MagneticField", deviceId, "MagneticField",
                              "Magnetic Field");
}

std::unique_ptr<Device> make_attitude(int deviceId) {
    return make_sensor_device("Attitude", deviceId, "Attitude", "Attitude");
}

std::unique_ptr<Device> make_light_sensor(int deviceId) {
    return make_sensor_device("Light", deviceId, "Light", "Light Sensor");
}

std::unique_ptr<Device> make_pressure_sensor(int deviceId) {
    return make_sensor_device("Pressure", deviceId, "Pressure",
                              "Pressure Sensor");
}

std::unique_ptr<Device> make_proximity_sensor(int deviceId) {
    return make_sensor_device("Proximity", deviceId, "Proximity",
                              "Proximity Sensor");
}

std::unique_ptr<Device> make_humidity_sensor(int deviceId) {
    return make_sensor_device("Humidity", deviceId, "Humidity",
                              "Humidity Sensor");
}

std::unique_ptr<Device> make_ambient_temperature_sensor(int deviceId) {
    return make_sensor_device("AmbientTemperature", deviceId,
                              "AmbientTemperature", "Ambient Temperature Sensor");
}

std::unique_ptr<Device> make_step_counter(int deviceId) {
    return make_sensor_device("StepCounter", deviceId, "StepCounter",
                              "Step Counter");
}

std::unique_ptr<Device> make_hinge_angle(int deviceId) {
    return make_sensor_device("HingeAngle", deviceId, "HingeAngle",
                              "Hinge Angle");
}

std::unique_ptr<Device> make_pen(int deviceId) {
    return make_sensor_device("Pen", deviceId, "Pen", "Pen");
}

std::unique_ptr<Device> make_tracked_device(int deviceId) {
    return make_sensor_device("TrackedDevice", deviceId, "TrackedDevice",
                              "Tracked Device");
}

std::unique_ptr<Device> make_touchscreen(int deviceId) {
    return make_sensor_device("Touchscreen", deviceId, "Touchscreen", "Touchscreen");
}

} // namespace insys



