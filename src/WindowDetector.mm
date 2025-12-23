#include "WindowDetector.h"

#include <QScreen>
#include <QDebug>

#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <AppKit/AppKit.h>

namespace {

// Classify element type based on window layer and characteristics
ElementType classifyElementType(int windowLayer, CFDictionaryRef windowInfo, CGRect bounds)
{
    // Layer 0-8: Normal windows and floating panels
    if (windowLayer <= 8) {
        return ElementType::Window;
    }

    // Layer 20-25: Menus, context menus, and popups
    if (windowLayer >= 20 && windowLayer <= 25) {
        // Context menus typically have no title
        CFStringRef nameRef = (CFStringRef)CFDictionaryGetValue(windowInfo, kCGWindowName);
        bool hasTitle = nameRef && CFStringGetLength(nameRef) > 0;

        // Check position - status bar popups appear near the top of screen (menu bar area)
        if (bounds.origin.y < 30) {
            return ElementType::StatusBarItem;
        }

        // Menus without titles are likely context menus
        if (!hasTitle) {
            return ElementType::ContextMenu;
        }

        return ElementType::PopupMenu;
    }

    // Layer > 25: Status bar items, control center, etc.
    if (windowLayer > 25 && windowLayer < 100) {
        return ElementType::StatusBarItem;
    }

    return ElementType::Unknown;
}

// Check if element type should be included based on detection flags
bool shouldIncludeElementType(ElementType type, DetectionFlags flags)
{
    switch (type) {
    case ElementType::Window:
        return flags.testFlag(DetectionFlag::Windows);
    case ElementType::ContextMenu:
        return flags.testFlag(DetectionFlag::ContextMenus);
    case ElementType::PopupMenu:
        return flags.testFlag(DetectionFlag::PopupMenus);
    case ElementType::Dialog:
        return flags.testFlag(DetectionFlag::Dialogs);
    case ElementType::StatusBarItem:
        return flags.testFlag(DetectionFlag::StatusBarItems);
    case ElementType::Unknown:
        return false;
    }
    return false;
}

// Get minimum size for element type
int getMinimumSize(ElementType type)
{
    switch (type) {
    case ElementType::Window:
        return 50;
    case ElementType::ContextMenu:
    case ElementType::PopupMenu:
    case ElementType::StatusBarItem:
        return 20;
    case ElementType::Dialog:
        return 30;
    case ElementType::Unknown:
        return 50;
    }
    return 50;
}

} // anonymous namespace

WindowDetector::WindowDetector(QObject *parent)
    : QObject(parent)
    , m_currentScreen(nullptr)
    , m_enabled(true)
    , m_detectionFlags(DetectionFlag::All)
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

void WindowDetector::setDetectionFlags(DetectionFlags flags)
{
    m_detectionFlags = flags;
}

DetectionFlags WindowDetector::detectionFlags() const
{
    return m_detectionFlags;
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
    // Determine CGWindowList options based on detection flags
    // When detecting system UI, we need to include all windows (not exclude desktop elements)
    CGWindowListOption options = kCGWindowListOptionOnScreenOnly;
    bool detectingSystemUI = m_detectionFlags & DetectionFlag::AllSystemUI;
    if (!detectingSystemUI) {
        options |= kCGWindowListExcludeDesktopElements;
    }

    CFArrayRef windowList = CGWindowListCopyWindowInfo(options, kCGNullWindowID);

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

        // Get window layer
        CFNumberRef layerRef = (CFNumberRef)CFDictionaryGetValue(windowInfo, kCGWindowLayer);
        int windowLayer = 0;
        if (layerRef) {
            CFNumberGetValue(layerRef, kCFNumberIntType, &windowLayer);
        }

        // Get window bounds early for element type classification
        CFDictionaryRef boundsDict = (CFDictionaryRef)CFDictionaryGetValue(windowInfo, kCGWindowBounds);
        if (!boundsDict) {
            continue;
        }

        CGRect cgBounds;
        if (!CGRectMakeWithDictionaryRepresentation(boundsDict, &cgBounds)) {
            continue;
        }

        // Classify element type based on layer and characteristics
        ElementType elementType = classifyElementType(windowLayer, windowInfo, cgBounds);

        // Check if this element type should be included based on detection flags
        if (!shouldIncludeElementType(elementType, m_detectionFlags)) {
            continue;
        }

        // Get minimum size based on element type
        int minSize = getMinimumSize(elementType);
        if (cgBounds.size.width < minSize || cgBounds.size.height < minSize) {
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
        element.elementType = elementType;

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
