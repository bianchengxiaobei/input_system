#include <catch2/catch_test_macros.hpp>

#include "insys/backend/android_input.h"
#include "insys/backend/mobile_input_queue.h"

using namespace insys::backend;

TEST_CASE("Android input attaches and detaches the mobile queue") {
    MobileInputQueue queue;
    android_input_detach();
    REQUIRE_FALSE(android_input_attached());
    REQUIRE(android_input_queue() == nullptr);

    android_input_attach(&queue);
    REQUIRE(android_input_attached());
    REQUIRE(android_input_queue() == &queue);

    android_input_detach();
    REQUIRE_FALSE(android_input_attached());
    REQUIRE(android_input_queue() == nullptr);
}
