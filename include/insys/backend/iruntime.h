#pragma once

#include "insys/backend/itime.h"
#include "insys/low/event.h"
#include "insys/types.h"

namespace insys {

class Manager;

enum class FocusState {
    Normal,
    Unfocused,
    Background,
};

enum class ScreenOrientation : int {
    Unknown = 0,
    Portrait = 1,
    PortraitUpsideDown = 2,
    LandscapeLeft = 3,
    LandscapeRight = 4,
    AutoRotation = 5,
};

struct DeviceCommand {
    FourCC type{};
    void* data = nullptr;
};

class IRuntime : public ITime {
public:
    ~IRuntime() override = default;

    virtual void set_manager(Manager* manager) = 0;
    virtual int allocate_device_id() = 0;
    virtual void queue_event(const low::Event* event) = 0;
    virtual long device_command(int deviceId, DeviceCommand* command) = 0;

    virtual double now_for_fixed() const = 0;
    virtual float unscaled_game_time() const = 0;
    virtual Vector2 screen_size() const = 0;
    virtual int screen_orientation() const = 0;
    virtual bool run_in_background() const = 0;
    virtual void set_run_in_background(bool value) = 0;
    virtual FocusState focus_state() const = 0;
    virtual void set_focus_state(FocusState value) = 0;
    virtual float polling_frequency() const = 0;
    virtual void set_polling_frequency(float value) = 0;
};

} // namespace insys
