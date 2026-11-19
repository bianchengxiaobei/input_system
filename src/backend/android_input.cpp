#include "insys/backend/android_input.h"

#include <atomic>

namespace insys::backend {
namespace {

std::atomic<MobileInputQueue*> g_queue{nullptr};

} // namespace

void android_input_attach(MobileInputQueue* queue) {
    g_queue.store(queue, std::memory_order_release);
}

void android_input_detach() {
    g_queue.store(nullptr, std::memory_order_release);
}

bool android_input_attached() {
    return g_queue.load(std::memory_order_acquire) != nullptr;
}

MobileInputQueue* android_input_queue() {
    return g_queue.load(std::memory_order_acquire);
}

} // namespace insys::backend
