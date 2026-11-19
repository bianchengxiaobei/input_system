#include <array>
#include <atomic>
#include <cstdlib>
#include <new>

#include <catch2/catch_test_macros.hpp>

#include "action_test_helpers.h"
#include "insys/actions/action_asset.h"
#include "insys/actions/action_state.h"

namespace {
std::atomic<std::size_t> g_allocationCount{0};
}

void* operator new(std::size_t size) {
    ++g_allocationCount;
    if (void* pointer = std::malloc(size))
        return pointer;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    ++g_allocationCount;
    if (void* pointer = std::malloc(size))
        return pointer;
    throw std::bad_alloc();
}

void operator delete(void* pointer) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

TEST_CASE("Steady-state event processing does not allocate") {
    insys_test::TestRuntime runtime;
    insys::Manager manager(runtime);
    insys::Keyboard* keyboard = insys_test::add_keyboard(manager);

    std::array<uint8_t, (insys::kKeyCount + 7) / 8> state{};
    const auto event = insys::low::make_state_event(
        insys::kFormatKeyboard, state.data(), static_cast<uint32_t>(state.size()),
        keyboard->deviceId, 1.0);

    // Warm up the queue, state buffers and any lazily-created scratch storage.
    for (int i = 0; i < 8; ++i) {
        manager.queue_event(reinterpret_cast<const insys::low::Event*>(event.data()));
        manager.update();
    }

    g_allocationCount.store(0, std::memory_order_relaxed);
    for (int i = 0; i < 1000; ++i) {
        manager.queue_event(reinterpret_cast<const insys::low::Event*>(event.data()));
        manager.update();
    }
    REQUIRE(g_allocationCount.load(std::memory_order_relaxed) == 0);
}
TEST_CASE("Action state updates in steady state do not allocate") {
    insys_test::TestRuntime runtime;
    insys::Manager manager(runtime);
    insys::Keyboard* keyboard = insys_test::add_keyboard(manager);

    insys::ActionAsset asset{"Player"};
    insys::ActionMap& map = asset.add_map("Player");
    map.add_action("Jump", insys::ActionType::Button);
    map.add_binding("Jump", "<Keyboard>/space");
    asset.enable(manager);

    std::array<uint8_t, (insys::kKeyCount + 7) / 8> pressed{};
    pressed[static_cast<std::size_t>(insys::Key::Space) / 8] |=
        static_cast<uint8_t>(1u << (static_cast<std::uint32_t>(insys::Key::Space) % 8));
    std::array<uint8_t, (insys::kKeyCount + 7) / 8> released{};
    const auto pressEvent = insys::low::make_state_event(
        insys::kFormatKeyboard, pressed.data(), static_cast<uint32_t>(pressed.size()),
        keyboard->deviceId, 2.0);
    const auto releaseEvent = insys::low::make_state_event(
        insys::kFormatKeyboard, released.data(), static_cast<uint32_t>(released.size()),
        keyboard->deviceId, 2.1);

    for (int i = 0; i < 8; ++i) {
        manager.queue_event(reinterpret_cast<const insys::low::Event*>(i == 0 ? pressEvent.data()
                                                                             : releaseEvent.data()));
        manager.update();
    }

    g_allocationCount.store(0, std::memory_order_relaxed);
    for (int i = 0; i < 1000; ++i) {
        manager.queue_event(reinterpret_cast<const insys::low::Event*>(i % 2 == 0
                                                                          ? pressEvent.data()
                                                                          : releaseEvent.data()));
        manager.update();
    }
    REQUIRE(g_allocationCount.load(std::memory_order_relaxed) == 0);
}