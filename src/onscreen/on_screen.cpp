#include "insys/onscreen/on_screen.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "insys/control_path.h"
#include "insys/low/event.h"

namespace insys::onscreen {

void OnScreenControl::set_control_path(std::string path) {
    m_controlPath = std::move(path);
}

std::string OnScreenControl::device_layout() const {
    const std::vector<PathComponent> components = parse_path(m_controlPath);
    if (components.empty())
        return {};
    if (!components[0].layout.empty())
        return components[0].layout.str();
    if (!components[0].name.empty())
        return components[0].name.str();
    return {};
}

bool OnScreenControl::bind(OnScreenInputModule& module, Device& device) {
    Control* control = find_control(device, m_controlPath);
    if (control == nullptr)
        return false;

    m_device = &device;
    m_control = control;
    m_sink = &module;
    return true;
}

void OnScreenControl::unbind() {
    m_device = nullptr;
    m_control = nullptr;
    m_sink = nullptr;
}

void OnScreenControl::reset_value() {
    if (m_control == nullptr || m_device == nullptr || m_sink == nullptr)
        return;

    const uint32_t size = m_control->value_size_bytes();
    if (size == 0)
        return;

    std::vector<uint8_t> zero(size, 0);
    m_sink->write_control_value(*m_device, *m_control, zero.data(), size);
}

void OnScreenButton::on_pointer_down(const ui::UiPointerEvent& event) {
    (void)event;
    send_float(1.0f);
}

void OnScreenButton::on_pointer_up(const ui::UiPointerEvent& event) {
    (void)event;
    send_float(0.0f);
}

void OnScreenStick::set_movement_range(float range) {
    m_movementRange = std::max(0.0f, range);
}

void OnScreenStick::set_dynamic_origin_range(float range) {
    m_dynamicOriginRange = std::max(0.0f, range);
}

bool OnScreenStick::accepts_pointer(Vector2 position) const {
    if (!contains(position))
        return false;
    if (m_behaviour == OnScreenStickBehaviour::ExactPositionWithDynamicOrigin &&
        (position - bounds().center()).length() > m_dynamicOriginRange)
        return false;
    return true;
}

void OnScreenStick::on_pointer_down(const ui::UiPointerEvent& event) {
    m_active = false;
    if (m_behaviour == OnScreenStickBehaviour::ExactPositionWithDynamicOrigin) {
        if ((event.position - bounds().center()).length() > m_dynamicOriginRange)
            return;
    }

    m_active = true;
    m_pointerDownPosition = event.position;
    if (m_behaviour == OnScreenStickBehaviour::ExactPositionWithStaticOrigin)
        update_stick(event.position);
    else
        send_vector2({});
}

void OnScreenStick::on_pointer_move(const ui::UiPointerEvent& event) {
    if (!m_active)
        return;
    update_stick(event.position);
}

void OnScreenStick::on_pointer_up(const ui::UiPointerEvent& event) {
    (void)event;
    m_active = false;
    reset_value();
}

void OnScreenStick::reset_value() {
    send_vector2({});
    m_pointerDownPosition = {};
    m_active = false;
}

void OnScreenStick::update_stick(Vector2 position) {
    if (m_movementRange <= 0.0f) {
        send_vector2({});
        return;
    }

    const Vector2 origin =
        m_behaviour == OnScreenStickBehaviour::ExactPositionWithStaticOrigin
            ? bounds().center()
            : m_pointerDownPosition;
    Vector2 delta = position - origin;
    const float length = delta.length();
    if (length > m_movementRange)
        delta = delta * (m_movementRange / length);
    send_vector2(delta / m_movementRange);
}

OnScreenInputModule::OnScreenInputModule(Manager& manager)
    : ui::UiPointerInputModule(manager) {}

OnScreenInputModule::~OnScreenInputModule() {
    for (OnScreenControl* control : m_controls) {
        if (control == nullptr)
            continue;
        control->reset_value();
        control->unbind();
    }
    m_controls.clear();
    clear_targets();

    for (const std::unique_ptr<DeviceGroup>& group : m_groups) {
        if (group->device != nullptr && m_manager != nullptr)
            m_manager->remove_device(group->device);
    }
    m_groups.clear();
    m_groupByDevice.clear();
}

bool OnScreenInputModule::add_control(OnScreenControl* control) {
    if (control == nullptr || m_manager == nullptr)
        return false;
    if (std::find(m_controls.begin(), m_controls.end(), control) != m_controls.end())
        return true;
    if (control->is_bound())
        return false;

    const std::string layout = control->device_layout();
    if (layout.empty())
        return false;

    DeviceGroup* group = ensure_group(layout);
    if (group == nullptr)
        return false;

    if (!control->bind(*this, *group->device)) {
        if (group->controlCount == 0)
            remove_group(group);
        return false;
    }

    ++group->controlCount;
    m_controls.push_back(control);
    add_target(control);
    return true;
}

void OnScreenInputModule::remove_control(OnScreenControl* control) {
    const auto it = std::find(m_controls.begin(), m_controls.end(), control);
    if (it == m_controls.end())
        return;

    Device* device = control->device();
    DeviceGroup* group = find_group(device);
    control->reset_value();
    control->unbind();
    remove_target(control);
    m_controls.erase(it);

    if (group == nullptr)
        return;
    if (group->controlCount > 0)
        --group->controlCount;
    if (group->controlCount == 0)
        remove_group(group);
    else if (group->dirty)
        flush();
}

Device* OnScreenInputModule::on_screen_device(std::string_view layout) const {
    DeviceGroup* group = find_group(layout);
    return group != nullptr ? group->device : nullptr;
}

void OnScreenInputModule::update() {
    ui::UiPointerInputModule::update();
    flush();
}

void OnScreenInputModule::flush() {
    if (m_manager == nullptr)
        return;

    const double now = m_manager->runtime().now();
    for (const std::unique_ptr<DeviceGroup>& group : m_groups) {
        if (!group->dirty || group->device == nullptr || group->state.empty())
            continue;

        const auto event = low::make_state_event(
            group->device->stateBlock.format, group->state.data(),
            static_cast<uint32_t>(group->state.size()), group->device->deviceId,
            now);
        m_manager->queue_event(reinterpret_cast<const low::Event*>(event.data()));
        group->dirty = false;
    }
}

void OnScreenInputModule::write_control_value(Device& device, Control& control,
                                              const void* value, uint32_t size) {
    (void)size;
    DeviceGroup* group = find_group(&device);
    if (group == nullptr || group->state.empty() || value == nullptr)
        return;

    control.write_value_to_state(group->state.data(), value);
    group->dirty = true;
}

OnScreenInputModule::DeviceGroup* OnScreenInputModule::find_group(
    Device* device) const {
    const auto it = m_groupByDevice.find(device);
    return it == m_groupByDevice.end() ? nullptr : it->second;
}

OnScreenInputModule::DeviceGroup* OnScreenInputModule::find_group(
    std::string_view layout) const {
    for (const std::unique_ptr<DeviceGroup>& group : m_groups) {
        if (group->layout == layout)
            return group.get();
    }
    return nullptr;
}

OnScreenInputModule::DeviceGroup* OnScreenInputModule::ensure_group(
    std::string_view layout) {
    if (layout.empty() || m_manager == nullptr)
        return nullptr;

    if (DeviceGroup* existing = find_group(layout))
        return existing;

    DeviceDescription description;
    description.interfaceName = "OnScreen";
    description.deviceClass = std::string(layout);
    description.product = "On-Screen " + std::string(layout);
    description.serial = "OnScreen:" + std::string(layout);

    Device* device = m_manager->create_device(layout, description);
    if (device == nullptr)
        return nullptr;

    auto group = std::make_unique<DeviceGroup>();
    group->device = device;
    group->layout = std::string(layout);
    group->state.assign(device->state_size_bytes(), 0);
    if (device->current_state() != nullptr && !group->state.empty())
        std::memcpy(group->state.data(), device->current_state(), group->state.size());
    group->dirty = false;
    group->controlCount = 0;

    DeviceGroup* raw = group.get();
    m_groups.push_back(std::move(group));
    m_groupByDevice[device] = raw;
    return raw;
}

void OnScreenInputModule::remove_group(DeviceGroup* group) {
    if (group == nullptr)
        return;

    if (group->device != nullptr && m_manager != nullptr)
        m_manager->remove_device(group->device);
    m_groupByDevice.erase(group->device);

    m_groups.erase(
        std::remove_if(m_groups.begin(), m_groups.end(),
                       [group](const std::unique_ptr<DeviceGroup>& candidate) {
                           return candidate.get() == group;
                       }),
        m_groups.end());
}

} // namespace insys::onscreen






