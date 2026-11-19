#pragma once

#include <cstdint>
#include <string>

#include "insys/backend/runtime.h"
#include "insys/backend/sensor_backend.h"
#include "insys/backend/touch_backend.h"
#include "insys/manager.h"

namespace insys::backend {

struct MobileMotionSample {
    Vector3 angularVelocity{};
    Vector3 acceleration{};
    Vector3 gravity{};
    Vector3 linearAcceleration{};
    Vector3 magneticField{};
    Quaternion attitude{};
    float lightLevel = 0.0f;
    float atmosphericPressure = 0.0f;
    float proximityDistance = 0.0f;
    float relativeHumidity = 0.0f;
    float ambientTemperature = 0.0f;
    float hingeAngle = 0.0f;
    int32_t stepCounter = 0;
    bool hasAngularVelocity = false;
    bool hasAcceleration = false;
    bool hasGravity = false;
    bool hasLinearAcceleration = false;
    bool hasMagneticField = false;
    bool hasAttitude = false;
    bool hasLightLevel = false;
    bool hasAtmosphericPressure = false;
    bool hasProximityDistance = false;
    bool hasRelativeHumidity = false;
    bool hasAmbientTemperature = false;
    bool hasHingeAngle = false;
    bool hasStepCounter = false;
};

class MobileInputBridge {
public:
    bool initialize(Runtime& runtime, Manager& manager,
                    const DeviceDescription& touchDescription = {});
    void shutdown();

    bool touch_began(int touchId, Vector2 position, float pressure = 1.0f);
    bool touch_moved(int touchId, Vector2 position, float pressure = 1.0f);
    bool touch_ended(int touchId);
    bool touch_canceled(int touchId);
    int suspend();

    bool update_gyroscope(Vector3 angularVelocity);
    bool update_accelerometer(Vector3 acceleration);
    bool update_gravity(Vector3 gravity);
    bool update_linear_acceleration(Vector3 acceleration);
    bool update_magnetic_field(Vector3 magneticField);
    bool update_attitude(Quaternion attitude);
    bool update_light(float lightLevel);
    bool update_pressure(float atmosphericPressure);
    bool update_proximity(float distance);
    bool update_humidity(float relativeHumidity);
    bool update_ambient_temperature(float ambientTemperature);
    bool update_hinge_angle(float angle);
    bool update_step_counter(int32_t stepCounter);
    bool update_motion(const MobileMotionSample& sample);

    void poll();

    TouchBackend& touch() { return m_touch; }
    SensorBackend& sensors() { return m_sensors; }

private:
    bool ensure_sensor(SensorType type);

    TouchBackend m_touch;
    SensorBackend m_sensors;
    DeviceDescription m_touchDescription;
    Runtime* m_runtime = nullptr;
    Manager* m_manager = nullptr;
};

} // namespace insys::backend





