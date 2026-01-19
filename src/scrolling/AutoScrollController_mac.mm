#include "scrolling/AutoScrollController.h"

#import <ApplicationServices/ApplicationServices.h>
#import <AppKit/AppKit.h>

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>

// Platform-specific free functions for macOS scroll injection
namespace AutoScrollPlatform {

bool injectScrollEvent(int deltaPixels, AutoScrollController::ScrollDirection dir, const QRect &region)
{
    // Create event source
    CGEventSourceRef eventSource = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    if (!eventSource) {
        qWarning() << "AutoScrollController: Failed to create event source";
        return false;
    }

    // Calculate target point (center of capture region)
    QPoint center = region.center();

    // Convert Qt coordinates to macOS screen coordinates
    // Qt uses top-left origin, macOS uses bottom-left for CGEvent
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        CFRelease(eventSource);
        return false;
    }

    qreal devicePixelRatio = screen->devicePixelRatio();
    int screenHeight = static_cast<int>(screen->geometry().height() * devicePixelRatio);

    // Target point in macOS coordinates
    CGPoint targetPoint = CGPointMake(
        center.x() * devicePixelRatio,
        screenHeight - (center.y() * devicePixelRatio));

    // Create scroll wheel event
    CGEventRef scrollEvent = nullptr;

    // For macOS, we primarily use pixel-based scrolling for smoother control
    // if using trackpad emulation, or line-based for mouse wheel.
    // CGEventCreateScrollWheelEvent uses units.
    
    int64_t wheelCount = 1;
    int32_t delta = 0;
    int32_t horizontalDelta = 0;
    
    // CGScrollEventUnitPixel ensures we scroll by pixels, not lines
    // Scrolling down requires negative delta for pixel units typically (or positive based on natural scrolling)
    // Actually, for CGEventCreateScrollWheelEvent:
    // "A positive value indicates scrolling up (or left), and a negative value indicates scrolling down (or right)."
    
    switch (dir) {
    case AutoScrollController::ScrollDirection::Down:
        delta = -deltaPixels;
        break;
    case AutoScrollController::ScrollDirection::Up:
        delta = deltaPixels;
        break;
    }

    scrollEvent = CGEventCreateScrollWheelEvent(
        eventSource,
        kCGScrollEventUnitPixel,
        wheelCount,
        delta);

    if (!scrollEvent) {
        qWarning() << "AutoScrollController: Failed to create scroll event";
        CFRelease(eventSource);
        return false;
    }

    // Set event location to center of capture region
    CGEventSetLocation(scrollEvent, targetPoint);

    // Post the event to the HID event stream
    CGEventPost(kCGHIDEventTap, scrollEvent);

    qDebug() << "AutoScrollController: Injected scroll event"
             << "delta:" << deltaPixels
             << "direction:" << static_cast<int>(dir)
             << "at:" << targetPoint.x << "," << targetPoint.y;

    CFRelease(scrollEvent);
    CFRelease(eventSource);

    return true;
}

bool isSupported()
{
    // macOS supports scroll injection via CGEventPost
    return true;
}

bool hasAccessibilityPermission()
{
    // Check if we have accessibility permission (required for CGEventPost)
    NSDictionary *options = @{
        (__bridge NSString *)kAXTrustedCheckOptionPrompt : @NO
    };
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

void requestAccessibilityPermission()
{
    // Request accessibility permission with prompt
    NSDictionary *options = @{
        (__bridge NSString *)kAXTrustedCheckOptionPrompt : @YES
    };
    AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);

    qDebug() << "AutoScrollController: Requested accessibility permission";
}

} // namespace AutoScrollPlatform

// Static method implementations that call the platform-specific functions
bool AutoScrollController::isSupported()
{
    return AutoScrollPlatform::isSupported();
}

bool AutoScrollController::hasAccessibilityPermission()
{
    return AutoScrollPlatform::hasAccessibilityPermission();
}

void AutoScrollController::requestAccessibilityPermission()
{
    AutoScrollPlatform::requestAccessibilityPermission();
}
