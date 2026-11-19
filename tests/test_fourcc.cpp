#include <catch2/catch_test_macros.hpp>

#include "insys/low/fourcc.h"
#include "insys/settings.h"
#include "insys/update_type.h"

using namespace insys;

TEST_CASE("FourCC packs and compares") {
    FourCC code('A', 'B', 'C', 'D');
    REQUIRE(code.value == 0x41424344u);
    REQUIRE(to_string(code) == "ABCD");

    FourCC padded('S', 'T', 'A', 'T');
    REQUIRE(padded.value == 0x53544154u);
    REQUIRE(padded == FourCC('S', 'T', 'A', 'T'));
    REQUIRE(padded != FourCC('D', 'L', 'T', 'A'));

    FourCC defaultPadded('K', 'E', 'Y');
    REQUIRE(to_string(defaultPadded) == "KEY ");
}

TEST_CASE("Update masks combine flags") {
    constexpr UpdateMask mask = static_cast<UpdateMask>(UpdateType::Dynamic) |
                                static_cast<UpdateMask>(UpdateType::Fixed);
    REQUIRE(has_update(mask, UpdateType::Dynamic));
    REQUIRE(has_update(mask, UpdateType::Fixed));
    REQUIRE_FALSE(has_update(mask, UpdateType::BeforeRender));
    REQUIRE(has_update(static_cast<UpdateMask>(UpdateType::Default), UpdateType::Dynamic));
    REQUIRE(has_update(static_cast<UpdateMask>(UpdateType::Default), UpdateType::Fixed));
}

TEST_CASE("Settings defaults match the design document") {
    Settings settings;
    REQUIRE(settings.defaultDeadzoneMin == 0.125f);
    REQUIRE(settings.defaultDeadzoneMax == 0.925f);
    REQUIRE(settings.defaultButtonPressPoint == 0.5f);
    REQUIRE(settings.buttonReleaseThreshold == 0.75f);
    REQUIRE(settings.defaultTapTime == 0.2f);
    REQUIRE(settings.defaultSlowTapTime == 0.5f);
    REQUIRE(settings.defaultHoldTime == 0.4f);
    REQUIRE(settings.tapRadius == 5.0f);
    REQUIRE(settings.multiTapDelayTime == 0.75f);
    REQUIRE(settings.maxQueuedEventsPerUpdate == 1000);
    REQUIRE(settings.maxEventBytesPerUpdate == 5 * 1024 * 1024);
    REQUIRE(settings.pollingFrequency == 60.0f);
    REQUIRE(settings.compensateForScreenOrientation);
    REQUIRE_FALSE(settings.shortcutKeysConsumeInput);
}
