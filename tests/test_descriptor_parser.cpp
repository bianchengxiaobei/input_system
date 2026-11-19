#include <catch2/catch_test_macros.hpp>

#include "insys/actions/descriptor_parser.h"

using namespace insys;

TEST_CASE("Descriptor parser handles empty and simple items") {
    REQUIRE(parse_list("").empty());

    const auto items = parse_list("Tap;Hold(duration=0.3)");
    REQUIRE(items.size() == 2);
    REQUIRE(items[0].name == "Tap");
    REQUIRE(items[0].parameters.empty());
    REQUIRE(items[1].name == "Hold");
    REQUIRE(items[1].parameters.size() == 1);
    REQUIRE(items[1].parameters[0].name == "duration");
    REQUIRE(items[1].parameters[0].value.as_float() == 0.3f);
}

TEST_CASE("Descriptor parser supports bools, ints and strings") {
    const auto item = parse_item("press(pressPoint=0.25,behavior=1,quoted=\"a,b\",flag)");
    REQUIRE(item.name == "press");
    REQUIRE(item.parameters.size() == 4);
    REQUIRE(item.parameters[0].value.as_float() == 0.25f);
    REQUIRE(item.parameters[1].value.as_int() == 1);
    REQUIRE(item.parameters[2].value.stringValue == "a,b");
    REQUIRE(item.parameters[3].name == "flag");
    REQUIRE(item.parameters[3].value.as_bool());
}

TEST_CASE("Descriptor parser handles composite syntax") {
    const auto items = parse_list("2DVector(mode=2);scale(factor=2)");
    REQUIRE(items.size() == 2);
    REQUIRE(items[0].name == "2DVector");
    REQUIRE(items[0].parameters.size() == 1);
    REQUIRE(items[0].parameters[0].value.as_int() == 2);
}
