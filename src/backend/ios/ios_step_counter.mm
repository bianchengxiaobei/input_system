#import <CoreMotion/CoreMotion.h>
#import <Foundation/Foundation.h>

#include "insys/backend/ios_step_counter.h"

#if defined(__APPLE__) && defined(TARGET_OS_IOS) && TARGET_OS_IOS

namespace insys::backend {
namespace {

CMPedometer* g_pedometer = nil;
IosStepCounterCallbacks g_callbacks{};
void* g_user = nullptr;
bool g_running = false;

void release_pedometer(CMPedometer* pedometer) {
#if !__has_feature(objc_arc)
    [pedometer release];
#endif
}

} // namespace

bool ios_step_counter_available() {
    return [CMPedometer isStepCountingAvailable] ? true : false;
}

IosMotionAuthorizationStatus ios_step_counter_authorization_status() {
    if (@available(iOS 11.0, *))
        return static_cast<IosMotionAuthorizationStatus>([CMPedometer authorizationStatus]);
    return IosMotionAuthorizationStatus::NotDetermined;
}

bool ios_step_counter_start(const IosStepCounterCallbacks& callbacks, void* user) {
    if (g_running || callbacks.data == nullptr || !ios_step_counter_available())
        return false;

    g_callbacks = callbacks;
    g_user = user;
    g_running = true;
    g_pedometer = [[CMPedometer alloc] init];

    [g_pedometer startPedometerUpdatesFromDate:[NSDate date]
                                   withHandler:^(CMPedometerData* data, NSError* error) {
        if (!g_running)
            return;
        if (error != nil) {
            NSLog(@"insys CMPedometer error: %@", [error localizedDescription]);
            return;
        }
        if (data == nil)
            return;

        const int32_t steps = static_cast<int32_t>(data.numberOfSteps.intValue);
        dispatch_async(dispatch_get_main_queue(), ^{
            if (g_running && g_callbacks.data != nullptr)
                g_callbacks.data(g_user, steps);
        });
    }];

    return true;
}

void ios_step_counter_stop() {
    if (g_pedometer == nil)
        return;
    g_running = false;
    [g_pedometer stopPedometerUpdates];
    release_pedometer(g_pedometer);
    g_pedometer = nil;
    g_callbacks = {};
    g_user = nullptr;
}

bool ios_step_counter_running() {
    return g_running;
}

} // namespace insys::backend

#endif
