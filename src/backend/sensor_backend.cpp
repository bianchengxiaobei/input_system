#include "insys/backend/sensor_backend.h"

#include <algorithm>
#include <utility>

#include "insys/device_states.h"
#include "insys/low/event.h"
#include "insys/low/state_format.h"

namespace insys::backend {
namespace {

const char* layout_name(SensorType type) {
    switch (type) {
    case SensorType::Gyroscope: return "Gyroscope";
    case SensorType::Accelerometer: return "Accelerometer";
    case SensorType::Gravity: return "Gravity";
    case SensorType::LinearAcceleration: return "LinearAcceleration";
    case SensorType::MagneticField: return "MagneticField";
    case SensorType::Attitude: return "Attitude";
    case SensorType::Light: return "Light";
    case SensorType::Pressure: return "Pressure";
    case SensorType::Proximity: return "Proximity";
    case SensorType::Humidity: return "Humidity";
    case SensorType::AmbientTemperature: return "AmbientTemperature";
    case SensorType::StepCounter: return "StepCounter";
    case SensorType::HingeAngle: return "HingeAngle";
    }
    return nullptr;
}

bool is_vector_type(SensorType type) {
    return type == SensorType::Gyroscope || type == SensorType::Accelerometer ||
           type == SensorType::Gravity ||
           type == SensorType::LinearAcceleration ||
           type == SensorType::MagneticField;
}

bool is_scalar_type(SensorType type) {
    return type == SensorType::Light || type == SensorType::Pressure ||
           type == SensorType::Proximity || type == SensorType::Humidity ||
           type == SensorType::AmbientTemperature ||
           type == SensorType::HingeAngle;
}

bool equal(const Quaternion& left, const Quaternion& right) {
    return left.x == right.x && left.y == right.y && left.z == right.z &&
           left.w == right.w;
}

} // namespace

bool SensorBackend::initialize(Runtime& runtime, Manager& manager) {
    if (m_runtime != nullptr)
        shutdown();
    m_runtime = &runtime;
    m_manager = &manager;
    m_slots.clear();
    m_dirtySensors.clear();
    return true;
}

void SensorBackend::shutdown() {
    if (m_manager != nullptr) {
        for (const auto& entry : m_slots) {
            if (Device* device = m_manager->device_by_id(entry.second.deviceId))
                m_manager->remove_device(device);
        }
    }
    m_slots.clear();
    m_dirtySensors.clear();
    m_runtime = nullptr;
    m_manager = nullptr;
}

bool SensorBackend::add_sensor(std::string id, SensorType type,
                               const DeviceDescription& description) {
    if (m_runtime == nullptr || m_manager == nullptr || id.empty() ||
        m_slots.find(id) != m_slots.end()) {
        return false;
    }

    const char* layout = layout_name(type);
    if (layout == nullptr)
        return false;

    DeviceDescription effective = description;
    if (effective.interfaceName.empty())
        effective.interfaceName = "Sensor";
    effective.deviceClass = layout;
    if (effective.product.empty())
        effective.product = std::string("Sensor ") + layout;
    if (effective.serial.empty())
        effective.serial = id;

    Device* device = m_manager->create_device(layout, effective);
    if (device == nullptr)
        return false;

    m_slots.emplace(std::move(id), Slot{type, device->deviceId});
    return true;
}

bool SensorBackend::remove_sensor(std::string_view id) {
    const auto it = m_slots.find(std::string(id));
    if (it == m_slots.end())
        return false;
    if (m_manager != nullptr) {
        if (Device* device = m_manager->device_by_id(it->second.deviceId))
            m_manager->remove_device(device);
    }
    m_dirtySensors.erase(
        std::remove(m_dirtySensors.begin(), m_dirtySensors.end(), id),
        m_dirtySensors.end());
    m_slots.erase(it);
    return true;
}

bool SensorBackend::update_gyroscope(std::string_view id, Vector3 angularVelocity) {
    return update_vector(id, SensorType::Gyroscope, angularVelocity);
}

bool SensorBackend::update_accelerometer(std::string_view id, Vector3 acceleration) {
    return update_vector(id, SensorType::Accelerometer, acceleration);
}

bool SensorBackend::update_gravity(std::string_view id, Vector3 gravity) {
    return update_vector(id, SensorType::Gravity, gravity);
}

bool SensorBackend::update_linear_acceleration(std::string_view id,
                                               Vector3 acceleration) {
    return update_vector(id, SensorType::LinearAcceleration, acceleration);
}

bool SensorBackend::update_magnetic_field(std::string_view id,
                                          Vector3 magneticField) {
    return update_vector(id, SensorType::MagneticField, magneticField);
}

bool SensorBackend::update_light(std::string_view id, float lightLevel) {
    return update_scalar(id, SensorType::Light, lightLevel);
}

bool SensorBackend::update_pressure(std::string_view id, float atmosphericPressure) {
    return update_scalar(id, SensorType::Pressure, atmosphericPressure);
}

bool SensorBackend::update_proximity(std::string_view id, float distance) {
    return update_scalar(id, SensorType::Proximity, distance);
}

bool SensorBackend::update_humidity(std::string_view id, float relativeHumidity) {
    return update_scalar(id, SensorType::Humidity, relativeHumidity);
}

bool SensorBackend::update_ambient_temperature(std::string_view id,
                                               float ambientTemperature) {
    return update_scalar(id, SensorType::AmbientTemperature, ambientTemperature);
}

bool SensorBackend::update_hinge_angle(std::string_view id, float angle) {
    return update_scalar(id, SensorType::HingeAngle, angle);
}

bool SensorBackend::update_step_counter(std::string_view id, int32_t stepCounter) {
    const auto it = m_slots.find(std::string(id));
    if (it == m_slots.end() || it->second.type != SensorType::StepCounter)
        return false;
    if (it->second.integer == stepCounter)
        return false;
    it->second.integer = stepCounter;
    mark_dirty(id, it->second);
    return true;
}

bool SensorBackend::update_attitude(std::string_view id, Quaternion attitude) {
    const auto it = m_slots.find(std::string(id));
    if (it == m_slots.end() || it->second.type != SensorType::Attitude)
        return false;
    if (equal(it->second.quaternion, attitude))
        return false;
    it->second.quaternion = attitude;
    mark_dirty(id, it->second);
    return true;
}

void SensorBackend::poll() {
    if (m_runtime == nullptr || m_manager == nullptr)
        return;

    for (const std::string& id : m_dirtySensors) {
        const auto it = m_slots.find(id);
        if (it == m_slots.end())
            continue;

        const int deviceId = it->second.deviceId;
        const double now = m_runtime->now();
        switch (it->second.type) {
        case SensorType::Gyroscope: {
            const GyroscopeState state{it->second.vector};
            const auto buffer = low::make_state_event(
                kFormatGyroscope, &state, static_cast<uint32_t>(sizeof(state)),
                deviceId, now);
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
            break;
        }
        case SensorType::Accelerometer: {
            const AccelerometerState state{it->second.vector};
            const auto buffer = low::make_state_event(
                kFormatAccelerometer, &state, static_cast<uint32_t>(sizeof(state)),
                deviceId, now);
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
            break;
        }
        case SensorType::Gravity: {
            const GravityState state{it->second.vector};
            const auto buffer = low::make_state_event(
                kFormatGravity, &state, static_cast<uint32_t>(sizeof(state)),
                deviceId, now);
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
            break;
        }
        case SensorType::LinearAcceleration: {
            const LinearAccelerationState state{it->second.vector};
            const auto buffer = low::make_state_event(
                kFormatLinearAcceleration, &state,
                static_cast<uint32_t>(sizeof(state)), deviceId, now);
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
            break;
        }
        case SensorType::MagneticField: {
            const MagneticFieldState state{it->second.vector};
            const auto buffer = low::make_state_event(
                kFormatMagneticField, &state, static_cast<uint32_t>(sizeof(state)),
                deviceId, now);
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
            break;
        }
        case SensorType::Attitude: {
            const AttitudeState state{it->second.quaternion};
            const auto buffer = low::make_state_event(
                kFormatAttitude, &state, static_cast<uint32_t>(sizeof(state)),
                deviceId, now);
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
            break;
        }
        case SensorType::Light: {
            const LightSensorState state{it->second.scalar};
            const auto buffer = low::make_state_event(
                kFormatLight, &state, static_cast<uint32_t>(sizeof(state)),
                deviceId, now);
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
            break;
        }
        case SensorType::Pressure: {
            const PressureSensorState state{it->second.scalar};
            const auto buffer = low::make_state_event(
                kFormatPressure, &state, static_cast<uint32_t>(sizeof(state)),
                deviceId, now);
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
            break;
        }
        case SensorType::Proximity: {
            const ProximitySensorState state{it->second.scalar};
            const auto buffer = low::make_state_event(
                kFormatProximity, &state, static_cast<uint32_t>(sizeof(state)),
                deviceId, now);
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
            break;
        }
        case SensorType::Humidity: {
            const HumiditySensorState state{it->second.scalar};
            const auto buffer = low::make_state_event(
                kFormatHumidity, &state, static_cast<uint32_t>(sizeof(state)),
                deviceId, now);
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
            break;
        }
        case SensorType::AmbientTemperature: {
            const AmbientTemperatureSensorState state{it->second.scalar};
            const auto buffer = low::make_state_event(
                kFormatAmbientTemperature, &state,
                static_cast<uint32_t>(sizeof(state)), deviceId, now);
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
            break;
        }
        case SensorType::StepCounter: {
            const StepCounterState state{it->second.integer};
            const auto buffer = low::make_state_event(
                kFormatStepCounter, &state, static_cast<uint32_t>(sizeof(state)),
                deviceId, now);
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
            break;
        }
        case SensorType::HingeAngle: {
            const HingeAngleState state{it->second.scalar};
            const auto buffer = low::make_state_event(
                kFormatHingeAngle, &state, static_cast<uint32_t>(sizeof(state)),
                deviceId, now);
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
            break;
        }
        }
        it->second.dirty = false;
    }
    m_dirtySensors.clear();
}

bool SensorBackend::connected(std::string_view id) const {
    return m_slots.find(std::string(id)) != m_slots.end();
}

int SensorBackend::device_id(std::string_view id) const {
    const auto it = m_slots.find(std::string(id));
    return it == m_slots.end() ? 0 : it->second.deviceId;
}

bool SensorBackend::update_vector(std::string_view id, SensorType type, Vector3 value) {
    const auto it = m_slots.find(std::string(id));
    if (it == m_slots.end() || it->second.type != type || !is_vector_type(type))
        return false;
    if (it->second.vector == value)
        return false;
    it->second.vector = value;
    mark_dirty(id, it->second);
    return true;
}

bool SensorBackend::update_scalar(std::string_view id, SensorType type,
                                 float value) {
    const auto it = m_slots.find(std::string(id));
    if (it == m_slots.end() || it->second.type != type || !is_scalar_type(type))
        return false;
    if (it->second.scalar == value)
        return false;
    it->second.scalar = value;
    mark_dirty(id, it->second);
    return true;
}

void SensorBackend::mark_dirty(std::string_view id, Slot& slot) {
    if (slot.dirty)
        return;
    slot.dirty = true;
    m_dirtySensors.emplace_back(id);
}

} // namespace insys::backend



