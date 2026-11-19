#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "insys/backend/runtime.h"
#include "insys/manager.h"

namespace insys::backend {

enum class SensorType {
    Gyroscope,
    Accelerometer,
    Gravity,
    LinearAcceleration,
    MagneticField,
    Attitude,
    Light,
    Pressure,
    Proximity,
    Humidity,
    AmbientTemperature,
    StepCounter,
    HingeAngle,
};

class SensorBackend {
public:
    bool initialize(Runtime& runtime, Manager& manager);
    void shutdown();

    bool add_sensor(std::string id, SensorType type,
                    const DeviceDescription& description = {});
    bool remove_sensor(std::string_view id);

    bool update_gyroscope(std::string_view id, Vector3 angularVelocity);
    bool update_accelerometer(std::string_view id, Vector3 acceleration);
    bool update_gravity(std::string_view id, Vector3 gravity);
    bool update_linear_acceleration(std::string_view id, Vector3 acceleration);
    bool update_magnetic_field(std::string_view id, Vector3 magneticField);
    bool update_attitude(std::string_view id, Quaternion attitude);
    bool update_light(std::string_view id, float lightLevel);
    bool update_pressure(std::string_view id, float atmosphericPressure);
    bool update_proximity(std::string_view id, float distance);
    bool update_humidity(std::string_view id, float relativeHumidity);
    bool update_ambient_temperature(std::string_view id, float ambientTemperature);
    bool update_step_counter(std::string_view id, int32_t stepCounter);
    bool update_hinge_angle(std::string_view id, float angle);
    void poll();

    bool connected(std::string_view id) const;
    int device_id(std::string_view id) const;

private:
    struct Slot {
        SensorType type = SensorType::Gyroscope;
        int deviceId = 0;
        Vector3 vector{};
        Quaternion quaternion{};
        float scalar = 0.0f;
        int32_t integer = 0;
        bool dirty = false;
    };

    bool update_vector(std::string_view id, SensorType type, Vector3 value);
    bool update_scalar(std::string_view id, SensorType type, float value);
    void mark_dirty(std::string_view id, Slot& slot);

    Runtime* m_runtime = nullptr;
    Manager* m_manager = nullptr;
    std::unordered_map<std::string, Slot> m_slots;
    std::vector<std::string> m_dirtySensors;
};

} // namespace insys::backend



