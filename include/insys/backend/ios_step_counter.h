#pragma once

#include <cstdint>

namespace insys::backend {

struct IosStepCounterCallbacks {
    void (*data)(void* user, int32_t numberOfSteps) = nullptr;
};

// Values match CoreMotion's CMAuthorizationStatus.
enum class IosMotionAuthorizationStatus : int {
    NotDetermined = 0,
    Restricted = 1,
    Denied = 2,
    Authorized = 3,
};

bool ios_step_counter_available();
IosMotionAuthorizationStatus ios_step_counter_authorization_status();
bool ios_step_counter_start(const IosStepCounterCallbacks& callbacks, void* user);
void ios_step_counter_stop();
bool ios_step_counter_running();

} // namespace insys::backend
