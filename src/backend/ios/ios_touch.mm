#import <TargetConditionals.h>
#import <UIKit/UIKit.h>

#include "insys/backend/ios_touch.h"

#if defined(__APPLE__) && defined(TARGET_OS_IOS) && TARGET_OS_IOS

#include <unordered_map>

namespace {

std::unordered_map<const void*, int> g_touchIds;
int g_nextTouchId = 1;
insys::backend::IosTouchCallbacks g_callbacks{};
void* g_user = nullptr;
UIView* g_view = nil;
UIGestureRecognizer* g_recognizer = nil;

const void* touch_key(UITouch* touch) {
#if __has_feature(objc_arc)
    return (__bridge const void*)touch;
#else
    return (const void*)touch;
#endif
}

float touch_pressure(UITouch* touch) {
    if (@available(iOS 9.0, *)) {
        if (touch.maximumPossibleForce > 0.0)
            return static_cast<float>(touch.force / touch.maximumPossibleForce);
    }
    return 1.0f;
}

} // namespace

@interface InsysTouchRecognizer : UIGestureRecognizer
@end

@implementation InsysTouchRecognizer

- (instancetype)init {
    self = [super initWithTarget:nil action:nil];
    if (self != nil) {
        self.cancelsTouchesInView = NO;
        self.delaysTouchesBegan = NO;
        self.delaysTouchesEnded = NO;
    }
    return self;
}

- (void)emitTouches:(NSSet<UITouch*>*)touches
              phase:(insys::backend::IosTouchPhase)phase {
    for (UITouch* touch in touches) {
        const void* key = touch_key(touch);
        auto found = g_touchIds.find(key);
        int id;
        if (found == g_touchIds.end()) {
            id = g_nextTouchId++;
            g_touchIds.emplace(key, id);
        } else {
            id = found->second;
        }

        insys::backend::IosTouchSample sample;
        sample.id = id;
        const CGPoint point = [touch locationInView:self.view];
        sample.position = {static_cast<float>(point.x), static_cast<float>(point.y)};
        sample.pressure = touch_pressure(touch);
        sample.phase = phase;
        if (g_callbacks.touch != nullptr)
            g_callbacks.touch(g_user, sample);

        if (phase == insys::backend::IosTouchPhase::Ended ||
            phase == insys::backend::IosTouchPhase::Canceled)
            g_touchIds.erase(key);
    }
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [super touchesBegan:touches withEvent:event];
    [self emitTouches:touches phase:insys::backend::IosTouchPhase::Began];
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [super touchesMoved:touches withEvent:event];
    [self emitTouches:touches phase:insys::backend::IosTouchPhase::Moved];
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [super touchesEnded:touches withEvent:event];
    [self emitTouches:touches phase:insys::backend::IosTouchPhase::Ended];
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [super touchesCancelled:touches withEvent:event];
    [self emitTouches:touches phase:insys::backend::IosTouchPhase::Canceled];
}

- (void)reset {
    [super reset];
    g_touchIds.clear();
}

@end

namespace insys::backend {

bool ios_touch_attach(void* uiView, const IosTouchCallbacks& callbacks, void* user) {
    if (uiView == nullptr || callbacks.touch == nullptr || g_recognizer != nil)
        return false;

#if __has_feature(objc_arc)
    UIView* view = (__bridge UIView*)uiView;
#else
    UIView* view = (UIView*)uiView;
#endif
    InsysTouchRecognizer* recognizer = [[InsysTouchRecognizer alloc] init];
    g_view = view;
    g_recognizer = recognizer;
    g_callbacks = callbacks;
    g_user = user;
    g_touchIds.clear();
    g_nextTouchId = 1;

#if !__has_feature(objc_arc)
    [g_view retain];
    [g_recognizer retain];
#endif
    [view addGestureRecognizer:recognizer];
#if !__has_feature(objc_arc)
    [recognizer release];
#endif
    return true;
}

void ios_touch_detach() {
    if (g_view != nil && g_recognizer != nil)
        [g_view removeGestureRecognizer:g_recognizer];
#if !__has_feature(objc_arc)
    [g_recognizer release];
    [g_view release];
#endif
    g_recognizer = nil;
    g_view = nil;
    g_callbacks = {};
    g_user = nullptr;
    g_touchIds.clear();
    g_nextTouchId = 1;
}

bool ios_touch_attached() {
    return g_recognizer != nil;
}

} // namespace insys::backend

#endif
