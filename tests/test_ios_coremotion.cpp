#include <catch2/catch_test_macros.hpp>

#include "insys/backend/ios_coremotion.h"

using namespace insys::backend;

TEST_CASE("iOS CoreMotion rejects an empty callback set") {
    IosCoreMotionCallbacks callbacks;
    REQUIRE_FALSE(ios_coremotion_start(callbacks, nullptr));
    REQUIRE_FALSE(ios_coremotion_running());
    ios_coremotion_stop();
    REQUIRE_FALSE(ios_coremotion_running());
}

#if !defined(__APPLE__) || !defined(TARGET_OS_IOS) || !TARGET_OS_IOS

TEST_CASE("iOS CoreMotion is unavailable outside iOS") {
    IosCoreMotionCallbacks callbacks;
    callbacks.gyroscope = [](void*, const IosGyroscopeSample&) {};
    REQUIRE_FALSE(ios_coremotion_start(callbacks, nullptr));
    REQUIRE_FALSE(ios_coremotion_running());
}

#endif
