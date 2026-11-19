#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "insys/control.h"
#include "insys/device_description.h"
#include "insys/low/key.h"

namespace insys {

class Device : public Control {
public:
    int deviceId = 0;
    int deviceIndex = -1;
    bool remote = false;
    int remoteParticipantId = 0;
    DeviceDescription description;
    uint32_t deviceFlags = 0;

    double lastUpdateTime = 0.0;
    uint32_t lastUpdateStepCount = 0;

    uint8_t* currentStatePtr = nullptr;
    uint8_t* previousStatePtr = nullptr;

    std::vector<std::unique_ptr<Control>> ownedControls;
    std::vector<std::string> layoutLineage;
    std::vector<Control*> allControls;
    std::vector<Control*> childrenFlat;

    uint32_t state_size_bytes() const;
    const uint8_t* current_state() const { return currentStatePtr; }

    uint32_t value_size_bytes() const override { return state_size_bytes(); }
    void read_value_from_state(const uint8_t* state, void* out) const override;
    void write_value_to_state(uint8_t* state, const void* in) const override;

    void finalize_hierarchy();

    Control* find_control(std::string_view controlName) const;
    Control* find_control_by_usage(InternedString usage) const;
};

class Keyboard : public Device {
public:
    Control* key_control(Key key) const;
    Control* any_key() const { return find_control("anyKey"); }
    Control* shift() const { return key_control(Key::LeftShift); }
    Control* ctrl() const { return key_control(Key::LeftCtrl); }
    Control* alt() const { return key_control(Key::LeftAlt); }
    Control* super() const { return key_control(Key::LeftSuper); }

    void rebuild_key_cache();

private:
    std::vector<Control*> m_keyCache;
};

class Mouse : public Device {
public:
    Vector2Control* position() const;
    Vector2Control* delta() const;
    Vector2Control* scroll() const;
    Control* left_button() const { return find_control("leftButton"); }
    Control* right_button() const { return find_control("rightButton"); }
    Control* middle_button() const { return find_control("middleButton"); }
    Control* forward_button() const { return find_control("forwardButton"); }
    Control* back_button() const { return find_control("backButton"); }
};

class Gamepad : public Device {
public:
    Control* left_stick() const { return find_control("leftStick"); }
    Control* right_stick() const { return find_control("rightStick"); }
    Control* left_trigger() const { return find_control("leftTrigger"); }
    Control* right_trigger() const { return find_control("rightTrigger"); }
    Control* dpad() const { return find_control("dpad"); }
    Control* button(std::string_view buttonName) const { return find_control(buttonName); }
};

class Joystick : public Device {
public:
    Control* stick() const { return find_control("stick"); }
    Control* trigger() const { return find_control("trigger"); }
};

class Gyroscope : public Device {
public:
    Vector3Control* angular_velocity() const {
        return static_cast<Vector3Control*>(find_control("angularVelocity"));
    }
};

class Accelerometer : public Device {
public:
    Vector3Control* acceleration() const {
        return static_cast<Vector3Control*>(find_control("acceleration"));
    }
};

class Gravity : public Device {
public:
    Vector3Control* gravity() const {
        return static_cast<Vector3Control*>(find_control("gravity"));
    }
};

class LinearAcceleration : public Device {
public:
    Vector3Control* acceleration() const {
        return static_cast<Vector3Control*>(find_control("acceleration"));
    }
};

class MagneticField : public Device {
public:
    Vector3Control* magnetic_field() const {
        return static_cast<Vector3Control*>(find_control("magneticField"));
    }
};

class Attitude : public Device {
public:
    QuaternionControl* attitude() const {
        return static_cast<QuaternionControl*>(find_control("attitude"));
    }
};

class LightSensor : public Device {
public:
    AxisControl* light_level() const {
        return static_cast<AxisControl*>(find_control("lightLevel"));
    }
};

class PressureSensor : public Device {
public:
    AxisControl* atmospheric_pressure() const {
        return static_cast<AxisControl*>(find_control("atmosphericPressure"));
    }
};

class ProximitySensor : public Device {
public:
    AxisControl* distance() const {
        return static_cast<AxisControl*>(find_control("distance"));
    }
};

class HumiditySensor : public Device {
public:
    AxisControl* relative_humidity() const {
        return static_cast<AxisControl*>(find_control("relativeHumidity"));
    }
};

class AmbientTemperatureSensor : public Device {
public:
    AxisControl* ambient_temperature() const {
        return static_cast<AxisControl*>(find_control("ambientTemperature"));
    }
};

class StepCounter : public Device {
public:
    IntegerControl* step_counter() const {
        return static_cast<IntegerControl*>(find_control("stepCounter"));
    }
};

class HingeAngle : public Device {
public:
    AxisControl* angle() const {
        return static_cast<AxisControl*>(find_control("angle"));
    }
};

class Pen : public Device {
public:
    Vector2Control* position() const {
        return static_cast<Vector2Control*>(find_control("position"));
    }
    DeltaControl* delta() const {
        return static_cast<DeltaControl*>(find_control("delta"));
    }
    Vector2Control* tilt() const {
        return static_cast<Vector2Control*>(find_control("tilt"));
    }
    AxisControl* pressure() const {
        return static_cast<AxisControl*>(find_control("pressure"));
    }
    AxisControl* twist() const {
        return static_cast<AxisControl*>(find_control("twist"));
    }
    Control* tip() const { return find_control("tip"); }
    Control* eraser() const { return find_control("eraser"); }
    Control* in_range() const { return find_control("inRange"); }
    Control* barrel_button(int index) const;
};

class TrackedDevice : public Device {
public:
    IntegerControl* tracking_state() const {
        return static_cast<IntegerControl*>(find_control("trackingState"));
    }
    Control* is_tracked() const { return find_control("isTracked"); }
    Vector3Control* device_position() const {
        return static_cast<Vector3Control*>(find_control("devicePosition"));
    }
    QuaternionControl* device_rotation() const {
        return static_cast<QuaternionControl*>(find_control("deviceRotation"));
    }
};

class Touchscreen : public Device {
public:
    Control* touch(int index) const;
    int touch_count() const { return 10; }
};

std::unique_ptr<Device> make_keyboard(int deviceId);
std::unique_ptr<Device> make_mouse(int deviceId);
std::unique_ptr<Device> make_gyroscope(int deviceId);
std::unique_ptr<Device> make_accelerometer(int deviceId);
std::unique_ptr<Device> make_gravity(int deviceId);
std::unique_ptr<Device> make_linear_acceleration(int deviceId);
std::unique_ptr<Device> make_magnetic_field(int deviceId);
std::unique_ptr<Device> make_attitude(int deviceId);
std::unique_ptr<Device> make_light_sensor(int deviceId);
std::unique_ptr<Device> make_pressure_sensor(int deviceId);
std::unique_ptr<Device> make_proximity_sensor(int deviceId);
std::unique_ptr<Device> make_humidity_sensor(int deviceId);
std::unique_ptr<Device> make_ambient_temperature_sensor(int deviceId);
std::unique_ptr<Device> make_step_counter(int deviceId);
std::unique_ptr<Device> make_hinge_angle(int deviceId);
std::unique_ptr<Device> make_pen(int deviceId);
std::unique_ptr<Device> make_tracked_device(int deviceId);
std::unique_ptr<Device> make_touchscreen(int deviceId);

} // namespace insys



