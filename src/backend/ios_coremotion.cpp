#include "insys/backend/ios_coremotion.h"

#if !defined(__APPLE__) || !defined(TARGET_OS_IOS) || !TARGET_OS_IOS

namespace insys::backend {

bool ios_coremotion_start(const IosCoreMotionCallbacks&, void*, double) {
    return false;
}

void ios_coremotion_stop() {}

bool ios_coremotion_running() {
    return false;
}

} // namespace insys::backend

#endif
