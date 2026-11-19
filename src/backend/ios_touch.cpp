#include "insys/backend/ios_touch.h"

namespace insys::backend {

bool feed_ios_touch(MobileInputBridge& bridge, const IosTouchSample& sample) {
    switch (sample.phase) {
    case IosTouchPhase::Began:
        return bridge.touch_began(sample.id, sample.position, sample.pressure);
    case IosTouchPhase::Moved:
        return bridge.touch_moved(sample.id, sample.position, sample.pressure);
    case IosTouchPhase::Ended:
        return bridge.touch_ended(sample.id);
    case IosTouchPhase::Canceled:
        return bridge.touch_canceled(sample.id);
    case IosTouchPhase::Stationary:
    default:
        return false;
    }
}

} // namespace insys::backend

#if !defined(__APPLE__) || !defined(TARGET_OS_IOS) || !TARGET_OS_IOS

namespace insys::backend {

bool ios_touch_attach(void*, const IosTouchCallbacks&, void*) {
    return false;
}

void ios_touch_detach() {}

bool ios_touch_attached() {
    return false;
}

} // namespace insys::backend

#endif
