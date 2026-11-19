#import <CoreMotion/CoreMotion.h>
#import <Foundation/Foundation.h>

#include "insys/backend/ios_coremotion.h"

#if defined(__APPLE__) && defined(TARGET_OS_IOS) && TARGET_OS_IOS

namespace insys::backend {
namespace {

CMMotionManager* g_manager = nil;
IosCoreMotionCallbacks g_callbacks{};
void* g_user = nullptr;
bool g_running = false;
bool g_deviceMotionStarted = false;
bool g_accelerometerStarted = false;
bool g_gyroscopeStarted = false;

void release_manager(CMMotionManager* manager) {
#if !__has_feature(objc_arc)
    [manager release];
#endif
}

void handle_error(NSError* error, const char* source) {
    if (error == nil)
        return;
    NSLog(@"insys CoreMotion %s error: %@", source, [error localizedDescription]);
}

} // namespace

bool ios_coremotion_start(const IosCoreMotionCallbacks& callbacks, void* user,
                          double interval) {
    if (g_running)
        return false;
    if (callbacks.deviceMotion == nullptr && callbacks.accelerometer == nullptr &&
        callbacks.gyroscope == nullptr)
        return false;

    CMMotionManager* manager = [[CMMotionManager alloc] init];
    const bool wantsDeviceMotion =
        callbacks.deviceMotion != nullptr && manager.deviceMotionAvailable;
    const bool wantsAccelerometer =
        callbacks.accelerometer != nullptr && manager.accelerometerAvailable;
    const bool wantsGyroscope =
        callbacks.gyroscope != nullptr && manager.gyroAvailable && !wantsDeviceMotion;
    if (!wantsDeviceMotion && !wantsAccelerometer && !wantsGyroscope) {
        release_manager(manager);
        return false;
    }

    g_manager = manager;
    g_callbacks = callbacks;
    g_user = user;
    g_deviceMotionStarted = wantsDeviceMotion;
    g_accelerometerStarted = wantsAccelerometer;
    g_gyroscopeStarted = wantsGyroscope;

    manager.deviceMotionUpdateInterval = interval;
    manager.accelerometerUpdateInterval = interval;
    manager.gyroUpdateInterval = interval;
    NSOperationQueue* queue = [NSOperationQueue mainQueue];
    g_running = true;

    if (wantsDeviceMotion) {
        [manager startDeviceMotionUpdatesToQueue:queue
                                    withHandler:^(CMDeviceMotion* motion, NSError* error) {
            handle_error(error, "deviceMotion");
            if (error != nil || motion == nil || !g_running)
                return;
            IosDeviceMotionSample sample;
            sample.hasRotationRate = true;
            sample.rotationRate = {
                static_cast<float>(motion.rotationRate.x),
                static_cast<float>(motion.rotationRate.y),
                static_cast<float>(motion.rotationRate.z),
            };
            sample.hasGravity = true;
            sample.gravity = {
                static_cast<float>(motion.gravity.x),
                static_cast<float>(motion.gravity.y),
                static_cast<float>(motion.gravity.z),
            };
            sample.hasUserAcceleration = true;
            sample.userAcceleration = {
                static_cast<float>(motion.userAcceleration.x),
                static_cast<float>(motion.userAcceleration.y),
                static_cast<float>(motion.userAcceleration.z),
            };
            const CMCalibratedMagneticField magneticField = motion.magneticField;
            sample.hasMagneticField = true;
            sample.magneticField = {
                static_cast<float>(magneticField.field.x),
                static_cast<float>(magneticField.field.y),
                static_cast<float>(magneticField.field.z),
            };
            const CMQuaternion q = motion.attitude.quaternion;
            sample.hasAttitude = true;
            sample.attitude = {
                static_cast<float>(q.x),
                static_cast<float>(q.y),
                static_cast<float>(q.z),
                static_cast<float>(q.w),
            };
            g_callbacks.deviceMotion(g_user, sample);
        }];
    }

    if (wantsAccelerometer) {
        [manager startAccelerometerUpdatesToQueue:queue
                                     withHandler:^(CMAccelerometerData* data, NSError* error) {
            handle_error(error, "accelerometer");
            if (error != nil || data == nil || !g_running)
                return;
            IosAccelerometerSample sample;
            sample.acceleration = {
                static_cast<float>(data.acceleration.x),
                static_cast<float>(data.acceleration.y),
                static_cast<float>(data.acceleration.z),
            };
            g_callbacks.accelerometer(g_user, sample);
        }];
    }

    if (wantsGyroscope) {
        [manager startGyroUpdatesToQueue:queue
                            withHandler:^(CMGyroData* data, NSError* error) {
            handle_error(error, "gyroscope");
            if (error != nil || data == nil || !g_running)
                return;
            IosGyroscopeSample sample;
            sample.rotationRate = {
                static_cast<float>(data.rotationRate.x),
                static_cast<float>(data.rotationRate.y),
                static_cast<float>(data.rotationRate.z),
            };
            g_callbacks.gyroscope(g_user, sample);
        }];
    }

    return true;
}

void ios_coremotion_stop() {
    if (g_manager == nil)
        return;
    g_running = false;
    if (g_deviceMotionStarted)
        [g_manager stopDeviceMotionUpdates];
    if (g_accelerometerStarted)
        [g_manager stopAccelerometerUpdates];
    if (g_gyroscopeStarted)
        [g_manager stopGyroUpdates];
    release_manager(g_manager);

    g_manager = nil;
    g_callbacks = {};
    g_user = nullptr;
    g_deviceMotionStarted = false;
    g_accelerometerStarted = false;
    g_gyroscopeStarted = false;
}

bool ios_coremotion_running() {
    return g_running;
}

} // namespace insys::backend

#endif
