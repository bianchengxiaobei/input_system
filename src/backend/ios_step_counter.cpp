#include "insys/backend/ios_step_counter.h"

#if !defined(__APPLE__) || !defined(TARGET_OS_IOS) || !TARGET_OS_IOS

namespace insys::backend {

bool ios_step_counter_available() {
    return false;
}

IosMotionAuthorizationStatus ios_step_counter_authorization_status() {
    return IosMotionAuthorizationStatus::NotDetermined;
}

bool ios_step_counter_start(const IosStepCounterCallbacks&, void*) {
    return false;
}

void ios_step_counter_stop() {}

bool ios_step_counter_running() {
    return false;
}

} // namespace insys::backend

#endif
