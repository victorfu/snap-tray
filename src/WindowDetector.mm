#include "WindowDetector.h"

#include <QScreen>
#include <QDebug>

#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <AppKit/AppKit.h>

WindowDetector::WindowDetector(QObject *parent)
    : QObject(parent)
    , m_currentScreen(nullptr)
    , m_enabled(true)
{
}

WindowDetector::~WindowDetector()
{
}

bool WindowDetector::hasAccessibilityPermission()
{
    // Check if the app has accessibility permissions (needed for some window info)
    // Note: Basic window enumeration via CGWindowList doesn't require accessibility,
    // but we check it anyway for potential future UI element detection
    NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @NO};
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

void WindowDetector::requestAccessibilityPermission()
{
    // Prompt user to grant accessibility permissions
    NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @YES};
    AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

void WindowDetector::setScreen(QScreen *screen)
{
    m_currentScreen = screen;
}

void WindowDetector::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool WindowDetector::isEnabled() const
{
    return m_enabled;
}

void WindowDetector::refreshWindowList()
{
    m_windowCache.clear();

    if (!m_enabled) {
        return;
    }

    enumerateWindows();

    qDebug() << "WindowDetector: Refreshed window list, found" << m_windowCache.size() << "windows";
}

void WindowDetector::enumerateWindows()
{
    // Get list of all on-screen windows
    CFArrayRef windowList = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID
    );

    if (!windowList) {
        qWarning() << "WindowDetector: Failed to get window list";
        return;
    }

    CFIndex count = CFArrayGetCount(windowList);

    // Get our own process ID to exclude our windows
    pid_t myPid = [[NSProcessInfo processInfo] processIdentifier];

    for (CFIndex i = 0; i < count; ++i) {
        CFDictionaryRef windowInfo = (CFDictionaryRef)CFArrayGetValueAtIndex(windowList, i);

        // Get window owner PID
        CFNumberRef pidRef = (CFNumberRef)CFDictionaryGetValue(windowInfo, kCGWindowOwnerPID);
        pid_t windowPid = 0;
        if (pidRef) {
            CFNumberGetValue(pidRef, kCFNumberIntType, &windowPid);
        }

        // Skip our own windows
        if (windowPid == myPid) {
            continue;
        }

        // Get window layer - skip windows that are not normal windows (e.g., menu bar, dock)
        CFNumberRef layerRef = (CFNumberRef)CFDictionaryGetValue(windowInfo, kCGWindowLayer);
        int windowLayer = 0;
        if (layerRef) {
            CFNumberGetValue(layerRef, kCFNumberIntType, &windowLayer);
        }

        // Layer 0 is normal windows, skip system UI elements (menu bar is layer 25, etc.)
        // We allow layers 0-8 which covers normal windows and some floating panels
        if (windowLayer > 8) {
            continue;
        }

        // Get window bounds
        CFDictionaryRef boundsDict = (CFDictionaryRef)CFDictionaryGetValue(windowInfo, kCGWindowBounds);
        if (!boundsDict) {
            continue;
        }

        CGRect cgBounds;
        if (!CGRectMakeWithDictionaryRepresentation(boundsDict, &cgBounds)) {
            continue;
        }

        // Skip windows that are too small (likely invisible or utility windows)
        if (cgBounds.size.width < 50 || cgBounds.size.height < 50) {
            continue;
        }

        // Get window ID
        CFNumberRef windowIdRef = (CFNumberRef)CFDictionaryGetValue(windowInfo, kCGWindowNumber);
        uint32_t windowId = 0;
        if (windowIdRef) {
            CFNumberGetValue(windowIdRef, kCFNumberSInt32Type, &windowId);
        }

        // Get window name (title)
        QString windowTitle;
        CFStringRef nameRef = (CFStringRef)CFDictionaryGetValue(windowInfo, kCGWindowName);
        if (nameRef) {
            NSString *name = (__bridge NSString *)nameRef;
            windowTitle = QString::fromNSString(name);
        }

        // Get owner application name
        QString ownerApp;
        CFStringRef ownerNameRef = (CFStringRef)CFDictionaryGetValue(windowInfo, kCGWindowOwnerName);
        if (ownerNameRef) {
            NSString *ownerName = (__bridge NSString *)ownerNameRef;
            ownerApp = QString::fromNSString(ownerName);
        }

        // Convert CGRect to QRect (CGRect uses top-left origin on macOS)
        QRect bounds(
            static_cast<int>(cgBounds.origin.x),
            static_cast<int>(cgBounds.origin.y),
            static_cast<int>(cgBounds.size.width),
            static_cast<int>(cgBounds.size.height)
        );

        DetectedElement element;
        element.bounds = bounds;
        element.windowTitle = windowTitle;
        element.ownerApp = ownerApp;
        element.windowLayer = windowLayer;
        element.windowId = windowId;

        m_windowCache.push_back(element);
    }

    CFRelease(windowList);
}

std::optional<DetectedElement> WindowDetector::detectWindowAt(const QPoint &screenPos) const
{
    if (!m_enabled || m_windowCache.empty()) {
        return std::nullopt;
    }

    // Filter to current screen if set
    QRect screenRect;
    if (m_currentScreen) {
        screenRect = m_currentScreen->geometry();
    }

    // Windows are already in z-order (topmost first) from CGWindowList
    for (const auto &window : m_windowCache) {
        // If filtering by screen, check if window intersects current screen
        if (!screenRect.isNull() && !window.bounds.intersects(screenRect)) {
            continue;
        }

        if (window.bounds.contains(screenPos)) {
            return window;
        }
    }

    return std::nullopt;
}
