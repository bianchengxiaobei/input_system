#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "insys/device.h"
#include "insys/manager.h"
#include "insys/types.h"
#include "insys/ui/ui_pointer_input.h"

namespace insys::onscreen {

class OnScreenInputModule;

class OnScreenControlSink {
public:
    virtual ~OnScreenControlSink() = default;
    virtual void write_control_value(Device& device, Control& control,
                                     const void* value, uint32_t size) = 0;
};

class OnScreenControl : public ui::UiPointerTarget {
public:
    ~OnScreenControl() override = default;

    void set_control_path(std::string path);
    const std::string& control_path() const { return m_controlPath; }
    std::string device_layout() const;

    void set_bounds(ui::UiRect bounds) { m_bounds = bounds; }
    const ui::UiRect& bounds() const { return m_bounds; }
    bool contains(Vector2 position) const override { return m_bounds.contains(position); }

    Control* control() const { return m_control; }
    Device* device() const { return m_device; }
    bool is_bound() const { return m_control != nullptr; }

    virtual void reset_value();

protected:
    template <typename T>
    void send_value(const T& value) {
        if (m_sink == nullptr || m_device == nullptr || m_control == nullptr)
            return;
        if (m_control->value_size_bytes() != sizeof(T))
            return;
        m_sink->write_control_value(*m_device, *m_control, &value, sizeof(T));
    }

    void send_float(float value) { send_value(value); }
    void send_vector2(Vector2 value) { send_value(value); }

private:
    friend class OnScreenInputModule;

    bool bind(OnScreenInputModule& module, Device& device);
    void unbind();

    std::string m_controlPath;
    ui::UiRect m_bounds{};
    Device* m_device = nullptr;
    Control* m_control = nullptr;
    OnScreenControlSink* m_sink = nullptr;
};

class OnScreenButton : public OnScreenControl {
public:
    OnScreenButton() = default;
    explicit OnScreenButton(std::string controlPath, ui::UiRect bounds = {}) {
        set_control_path(std::move(controlPath));
        set_bounds(bounds);
    }

    void on_pointer_down(const ui::UiPointerEvent& event) override;
    void on_pointer_up(const ui::UiPointerEvent& event) override;
};

enum class OnScreenStickBehaviour {
    RelativePositionWithStaticOrigin,
    ExactPositionWithStaticOrigin,
    ExactPositionWithDynamicOrigin,
};

class OnScreenStick : public OnScreenControl {
public:
    OnScreenStick() = default;
    explicit OnScreenStick(std::string controlPath, ui::UiRect bounds = {}) {
        set_control_path(std::move(controlPath));
        set_bounds(bounds);
    }

    void set_movement_range(float range);
    float movement_range() const { return m_movementRange; }
    void set_dynamic_origin_range(float range);
    float dynamic_origin_range() const { return m_dynamicOriginRange; }
    void set_behaviour(OnScreenStickBehaviour behaviour) { m_behaviour = behaviour; }
    OnScreenStickBehaviour behaviour() const { return m_behaviour; }

    bool accepts_pointer(Vector2 position) const override;
    void on_pointer_down(const ui::UiPointerEvent& event) override;
    void on_pointer_move(const ui::UiPointerEvent& event) override;
    void on_pointer_up(const ui::UiPointerEvent& event) override;
    void reset_value() override;

private:
    void update_stick(Vector2 position);

    float m_movementRange = 50.0f;
    float m_dynamicOriginRange = 100.0f;
    OnScreenStickBehaviour m_behaviour =
        OnScreenStickBehaviour::RelativePositionWithStaticOrigin;
    Vector2 m_pointerDownPosition{};
    bool m_active = false;
};

class OnScreenInputModule : public ui::UiPointerInputModule,
                            public OnScreenControlSink {
public:
    explicit OnScreenInputModule(Manager& manager);
    ~OnScreenInputModule() override;

    bool add_control(OnScreenControl* control);
    void remove_control(OnScreenControl* control);
    Device* on_screen_device(std::string_view layout) const;

    void update() override;
    void flush();

    void write_control_value(Device& device, Control& control,
                             const void* value, uint32_t size) override;

private:
    struct DeviceGroup {
        Device* device = nullptr;
        std::string layout;
        std::vector<uint8_t> state;
        bool dirty = false;
        int controlCount = 0;
    };

    DeviceGroup* find_group(Device* device) const;
    DeviceGroup* find_group(std::string_view layout) const;
    DeviceGroup* ensure_group(std::string_view layout);
    void remove_group(DeviceGroup* group);

    std::vector<std::unique_ptr<DeviceGroup>> m_groups;
    std::unordered_map<Device*, DeviceGroup*> m_groupByDevice;
    std::vector<OnScreenControl*> m_controls;
};

} // namespace insys::onscreen





