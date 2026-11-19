#include "insys/backend/mobile_input.h"

#include <utility>

namespace insys::backend {
namespace {

const char* sensor_id(SensorType type) {
    switch (type) {
    case SensorType::Gyroscope: return "MobileGyroscope";
    case SensorType::Accelerometer: return "MobileAccelerometer";
    case SensorType::Gravity: return "MobileGravity";
    case SensorType::LinearAcceleration: return "MobileLinearAcceleration";
    case SensorType::MagneticField: return "MobileMagneticField";
    case SensorType::Attitude: return "MobileAttitude";
    case SensorType::Light: return "MobileLight";
    case SensorType::Pressure: return "MobilePressure";
    case SensorType::Proximity: return "MobileProximity";
    case SensorType::Humidity: return "MobileHumidity";
    case SensorType::AmbientTemperature: return "MobileAmbientTemperature";
    case SensorType::StepCounter: return "MobileStepCounter";
    case SensorType::HingeAngle: return "MobileHingeAngle";
    }
    return "MobileSensor";
}

const char* sensor_product(SensorType type) {
    switch (type) {
    case SensorType::Gyroscope: return "Mobile Gyroscope";
    case SensorType::Accelerometer: return "Mobile Accelerometer";
    case SensorType::Gravity: return "Mobile Gravity";
    case SensorType::LinearAcceleration: return "Mobile Linear Acceleration";
    case SensorType::MagneticField: return "Mobile Magnetic Field";
    case SensorType::Attitude: return "Mobile Attitude";
    case SensorType::Light: return "Mobile Light";
    case SensorType::Pressure: return "Mobile Pressure";
    case SensorType::Proximity: return "Mobile Proximity";
    case SensorType::Humidity: return "Mobile Humidity";
    case SensorType::AmbientTemperature: return "Mobile Ambient Temperature";
    case SensorType::StepCounter: return "Mobile Step Counter";
    case SensorType::HingeAngle: return "Mobile Hinge Angle";
    }
    return "Mobile Sensor";
}

} // namespace

bool MobileInputBridge::initialize(Runtime& runtime, Manager& manager,
                                   const DeviceDescription& touchDescription) {
    if (m_runtime != nullptr)
        shutdown();

    m_touchDescription = touchDescription;
    if (!m_touch.initialize(runtime, manager, m_touchDescription) ||
        !m_sensors.initialize(runtime, manager)) {
        return false;
    }

    m_runtime = &runtime;
    m_manager = &manager;
    return true;
}

void MobileInputBridge::shutdown() {
    m_touch.shutdown();
    m_sensors.shutdown();
    m_touchDescription = {};
    m_runtime = nullptr;
    m_manager = nullptr;
}

bool MobileInputBridge::touch_began(int touchId, Vector2 position, float pressure) {
    return m_touch.touch_began(touchId, position, pressure);
}

bool MobileInputBridge::touch_moved(int touchId, Vector2 position, float pressure) {
    return m_touch.touch_moved(touchId, position, pressure);
}

bool MobileInputBridge::touch_ended(int touchId) {
    return m_touch.touch_ended(touchId);
}

bool MobileInputBridge::touch_canceled(int touchId) {
    return m_touch.touch_canceled(touchId);
}

int MobileInputBridge::suspend() {
    return m_touch.cancel_all();
}

bool MobileInputBridge::update_gyroscope(Vector3 angularVelocity) {
    return ensure_sensor(SensorType::Gyroscope) &&
           m_sensors.update_gyroscope(sensor_id(SensorType::Gyroscope), angularVelocity);
}

bool MobileInputBridge::update_accelerometer(Vector3 acceleration) {
    return ensure_sensor(SensorType::Accelerometer) &&
           m_sensors.update_accelerometer(sensor_id(SensorType::Accelerometer),
                                          acceleration);
}

bool MobileInputBridge::update_gravity(Vector3 gravity) {
    return ensure_sensor(SensorType::Gravity) &&
           m_sensors.update_gravity(sensor_id(SensorType::Gravity), gravity);
}

bool MobileInputBridge::update_linear_acceleration(Vector3 acceleration) {
    return ensure_sensor(SensorType::LinearAcceleration) &&
           m_sensors.update_linear_acceleration(
               sensor_id(SensorType::LinearAcceleration), acceleration);
}

bool MobileInputBridge::update_magnetic_field(Vector3 magneticField) {
    return ensure_sensor(SensorType::MagneticField) &&
           m_sensors.update_magnetic_field(
               sensor_id(SensorType::MagneticField), magneticField);
}

bool MobileInputBridge::update_attitude(Quaternion attitude) {
    return ensure_sensor(SensorType::Attitude) &&
           m_sensors.update_attitude(sensor_id(SensorType::Attitude), attitude);
}

bool MobileInputBridge::update_light(float lightLevel) {
    return ensure_sensor(SensorType::Light) &&
           m_sensors.update_light(sensor_id(SensorType::Light), lightLevel);
}

bool MobileInputBridge::update_pressure(float atmosphericPressure) {
    return ensure_sensor(SensorType::Pressure) &&
           m_sensors.update_pressure(sensor_id(SensorType::Pressure),
                                     atmosphericPressure);
}

bool MobileInputBridge::update_proximity(float distance) {
    return ensure_sensor(SensorType::Proximity) &&
           m_sensors.update_proximity(sensor_id(SensorType::Proximity), distance);
}

bool MobileInputBridge::update_humidity(float relativeHumidity) {
    return ensure_sensor(SensorType::Humidity) &&
           m_sensors.update_humidity(sensor_id(SensorType::Humidity),
                                     relativeHumidity);
}

bool MobileInputBridge::update_ambient_temperature(float ambientTemperature) {
    return ensure_sensor(SensorType::AmbientTemperature) &&
           m_sensors.update_ambient_temperature(
               sensor_id(SensorType::AmbientTemperature), ambientTemperature);
}

bool MobileInputBridge::update_hinge_angle(float angle) {
    return ensure_sensor(SensorType::HingeAngle) &&
           m_sensors.update_hinge_angle(sensor_id(SensorType::HingeAngle), angle);
}

bool MobileInputBridge::update_step_counter(int32_t stepCounter) {
    return ensure_sensor(SensorType::StepCounter) &&
           m_sensors.update_step_counter(sensor_id(SensorType::StepCounter),
                                         stepCounter);
}

bool MobileInputBridge::update_motion(const MobileMotionSample& sample) {
    bool changed = false;
    if (sample.hasAngularVelocity)
        changed = update_gyroscope(sample.angularVelocity) || changed;
    if (sample.hasAcceleration)
        changed = update_accelerometer(sample.acceleration) || changed;
    if (sample.hasGravity)
        changed = update_gravity(sample.gravity) || changed;
    if (sample.hasLinearAcceleration)
        changed = update_linear_acceleration(sample.linearAcceleration) || changed;
    if (sample.hasMagneticField)
        changed = update_magnetic_field(sample.magneticField) || changed;
    if (sample.hasAttitude)
        changed = update_attitude(sample.attitude) || changed;
    if (sample.hasLightLevel)
        changed = update_light(sample.lightLevel) || changed;
    if (sample.hasAtmosphericPressure)
        changed = update_pressure(sample.atmosphericPressure) || changed;
    if (sample.hasProximityDistance)
        changed = update_proximity(sample.proximityDistance) || changed;
    if (sample.hasRelativeHumidity)
        changed = update_humidity(sample.relativeHumidity) || changed;
    if (sample.hasAmbientTemperature)
        changed = update_ambient_temperature(sample.ambientTemperature) || changed;
    if (sample.hasHingeAngle)
        changed = update_hinge_angle(sample.hingeAngle) || changed;
    if (sample.hasStepCounter)
        changed = update_step_counter(sample.stepCounter) || changed;
    return changed;
}

void MobileInputBridge::poll() {
    m_touch.poll();
    m_sensors.poll();
}

bool MobileInputBridge::ensure_sensor(SensorType type) {
    if (m_runtime == nullptr || m_manager == nullptr)
        return false;

    const char* id = sensor_id(type);
    if (m_sensors.connected(id))
        return true;

    DeviceDescription description;
    description.interfaceName = "MobileSensor";
    description.product = sensor_product(type);
    description.serial = id;
    return m_sensors.add_sensor(id, type, description);
}

} // namespace insys::backend


