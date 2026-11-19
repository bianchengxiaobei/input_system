#include "insys/backend/android_input.h"

#if defined(__ANDROID__)

#include <jni.h>

#include <algorithm>
#include <cstddef>

#include "insys/backend/android_sensor.h"
#include "insys/backend/mobile_input_queue.h"

namespace {

insys::backend::MobileInputQueue* attached_queue() {
    return insys::backend::android_input_queue();
}

bool push_touch(jint id, jfloat x, jfloat y, jfloat pressure, jint phase) {
    auto* queue = attached_queue();
    if (queue == nullptr)
        return false;

    using insys::backend::AndroidTouchPhase;
    using insys::backend::MobileInputEventType;
    MobileInputEventType type;
    switch (static_cast<AndroidTouchPhase>(phase)) {
    case AndroidTouchPhase::Began:
        type = MobileInputEventType::TouchBegan;
        break;
    case AndroidTouchPhase::Moved:
        type = MobileInputEventType::TouchMoved;
        break;
    case AndroidTouchPhase::Ended:
        type = MobileInputEventType::TouchEnded;
        break;
    case AndroidTouchPhase::Canceled:
        type = MobileInputEventType::TouchCanceled;
        break;
    case AndroidTouchPhase::Stationary:
    default:
        return false;
    }

    queue->push_touch(type, static_cast<int>(id), {x, y}, pressure);
    return true;
}

bool push_sensor(jint type, const jfloat* values, jsize count) {
    auto* queue = attached_queue();
    if (queue == nullptr || values == nullptr)
        return false;

    insys::backend::AndroidSensorEvent event;
    event.type = static_cast<insys::backend::AndroidSensorType>(type);
    const jsize copied = std::min<jsize>(count, 16);
    for (jsize i = 0; i < copied; ++i)
        event.values[static_cast<std::size_t>(i)] = values[i];

    insys::MobileMotionSample sample;
    if (!insys::backend::make_android_motion_sample(event, sample))
        return false;
    queue->push_motion(sample);
    return true;
}

} // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_com_insys_input_MobileInput_nativePushTouch(JNIEnv*, jclass, jint id,
                                                 jfloat x, jfloat y,
                                                 jfloat pressure, jint phase) {
    push_touch(id, x, y, pressure, phase);
}

JNIEXPORT void JNICALL
Java_com_insys_input_MobileInput_nativePushSensor(JNIEnv* env, jclass, jint type,
                                                  jfloatArray values) {
    if (values == nullptr)
        return;
    const jsize count = env->GetArrayLength(values);
    jfloat buffer[16]{};
    if (count > 0) {
        const jsize copied = std::min<jsize>(count, 16);
        env->GetFloatArrayRegion(values, 0, copied, buffer);
    }
    push_sensor(type, buffer, count);
}

JNIEXPORT void JNICALL
Java_com_insys_input_MobileInput_nativeSuspend(JNIEnv*, jclass) {
    if (auto* queue = attached_queue())
        queue->push_suspend();
}

} // extern "C"

#endif
