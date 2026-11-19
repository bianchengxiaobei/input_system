#include <catch2/catch_test_macros.hpp>

#include "insys/backend/linux_evdev.h"

using namespace insys::backend;

TEST_CASE("Linux evdev platform API has a safe fallback") {
#if !defined(__linux__)
    REQUIRE(linux_evdev::scan_devices().empty());
    const EvdevBackend::SourceFactory factory = linux_evdev::make_source_factory();
    REQUIRE_FALSE(factory);
#else
    SUCCEED("Linux evdev platform API available");
#endif
}
