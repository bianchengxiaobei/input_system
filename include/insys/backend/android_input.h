#pragma once

namespace insys::backend {

class MobileInputQueue;

enum class AndroidTouchPhase : int {
    Began = 0,
    Moved = 1,
    Stationary = 2,
    Ended = 3,
    Canceled = 4,
};

// Attaches the queue used by the JNI entry points. Call from native code
// after creating MobileInputQueue; detach before destroying it.
void android_input_attach(MobileInputQueue* queue);
void android_input_detach();
bool android_input_attached();

// Internal accessor used by the Android JNI translation unit.
MobileInputQueue* android_input_queue();

} // namespace insys::backend
