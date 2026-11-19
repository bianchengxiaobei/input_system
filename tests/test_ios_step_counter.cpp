#include <catch2/catch_test_macros.hpp>

#include "insys/backend/ios_step_counter.h"

using namespace insys::backend;

TEST_CASE("iOS step counter is unavailable off iOS") {
    REQUIRE_FALSE(ios_step_counter_available());
    REQUIRE(ios_step_counter_authorization_status() ==
            IosMotionAuthorizationStatus::NotDetermined);
    REQUIRE_FALSE(ios_step_counter_start({}, nullptr));
    REQUIRE_FALSE(ios_step_counter_running());
    ios_step_counter_stop();
    REQUIRE_FALSE(ios_step_counter_running());
}
