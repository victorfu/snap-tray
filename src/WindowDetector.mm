#include "WindowDetector.h"

#include <QScreen>
#include <QDebug>
#include <QtConcurrent>
#include <unistd.h>

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

    // Layer 26-99: Status bar items, control center, etc.
    if (windowLayer > 25 && windowLayer < 100) {
        return ElementType::StatusBarItem;
    }

    // Layer 100-105: Context Menus, Popups (NSPopUpMenuWindowLevel = 101)
    if (windowLayer >= 100 && windowLayer <= 105) {
        CFStringRef nameRef = (CFStringRef)CFDictionaryGetValue(windowInfo, kCGWindowName);
        bool hasTitle = nameRef && CFStringGetLength(nameRef) > 0;

        // Context menus typically have no title
        if (!hasTitle) {
            return ElementType::ContextMenu;
        }
        return ElementType::PopupMenu;
    }

    // Layer > 105: System dialogs, alerts, and other high-priority windows
    if (windowLayer > 105) {
        return ElementType::Dialog;
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

// Get element frame (position + size) from an AXUIElement
bool getAXElementFrame(AXUIElementRef element, CGRect &outFrame)
{
    CFTypeRef posRef = nullptr;
    CFTypeRef sizeRef = nullptr;

    AXError err = AXUIElementCopyAttributeValue(element, kAXPositionAttribute, &posRef);
    if (err != kAXErrorSuccess || !posRef) return false;

    if (CFGetTypeID(posRef) != AXValueGetTypeID()) {
        CFRelease(posRef);
        return false;
    }

    err = AXUIElementCopyAttributeValue(element, kAXSizeAttribute, &sizeRef);
    if (err != kAXErrorSuccess || !sizeRef) {
        CFRelease(posRef);
        return false;
    }

    if (CFGetTypeID(sizeRef) != AXValueGetTypeID()) {
        CFRelease(posRef);
        CFRelease(sizeRef);
        return false;
    }

    CGPoint origin;
    CGSize size;
    Boolean gotPos = AXValueGetValue((AXValueRef)posRef, (AXValueType)kAXValueCGPointType, &origin);
    Boolean gotSize = AXValueGetValue((AXValueRef)sizeRef, (AXValueType)kAXValueCGSizeType, &size);

    CFRelease(posRef);
    CFRelease(sizeRef);

    if (!gotPos || !gotSize) return false;

    outFrame = CGRectMake(origin.x, origin.y, size.width, size.height);
    return true;
}

// Get the accessibility role string from an AXUIElement
QString getAXElementRole(AXUIElementRef element)
{
    CFTypeRef roleRef = nullptr;
    AXError err = AXUIElementCopyAttributeValue(element, kAXRoleAttribute, &roleRef);
    if (err != kAXErrorSuccess || !roleRef) return QString();

    if (CFGetTypeID(roleRef) != CFStringGetTypeID()) {
        CFRelease(roleRef);
        return QString();
    }

    QString role = QString::fromCFString((CFStringRef)roleRef);
    CFRelease(roleRef);
    return role;
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
    if (m_refreshFuture.isValid() && m_refreshFuture.isRunning()) {
        m_refreshFuture.waitForFinished();
    }
}

bool WindowDetector::hasAccessibilityPermission()
{
    // Check if the app has accessibility permissions (needed for some window info)
    // Note: Basic window enumeration via CGWindowList doesn't require accessibility,
    // but we check it anyway for potential future UI element detection
    NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @NO};
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
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

DetectionFlags WindowDetector::detectionFlags() const
{
    return m_detectionFlags;
}

void WindowDetector::refreshWindowList()
{
    QMutexLocker locker(&m_cacheMutex);
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
        element.nativeHandle = static_cast<quintptr>(windowId);
        element.elementType = elementType;
        element.ownerPid = windowPid;

        m_windowCache.push_back(element);
    }

    CFRelease(windowList);
}

std::optional<DetectedElement> WindowDetector::detectChildElementAt(
    const QPoint &screenPos, qint64 targetPid, const QRect &windowBounds) const
{
    if (!hasAccessibilityPermission()) {
        return std::nullopt;
    }

    // Return cached result if cursor is still within bounds and cache is fresh (<200ms)
    if (m_axCacheValid &&
        m_axCache.bounds.contains(screenPos) &&
        m_axCacheTimer.elapsed() < 200) {
        return m_axCache;
    }

    // Create application-scoped element for targeted hit-testing.
    // Using AXUIElementCreateApplication(pid) instead of system-wide avoids
    // hitting SnapTray's own full-screen overlay during region capture.
    AXUIElementRef appElement = AXUIElementCreateApplication(static_cast<pid_t>(targetPid));
    AXUIElementSetMessagingTimeout(appElement, 0.05); // 50ms timeout

    AXUIElementRef hitElement = nullptr;
    AXError err = AXUIElementCopyElementAtPosition(
        appElement, static_cast<float>(screenPos.x()), static_cast<float>(screenPos.y()), &hitElement);
    CFRelease(appElement);

    if (err != kAXErrorSuccess || !hitElement) {
        m_axCacheValid = false;
        return std::nullopt;
    }

    // Look up owning application name
    QString ownerApp;
    NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:static_cast<pid_t>(targetPid)];
    if (app.localizedName) {
        ownerApp = QString::fromNSString(app.localizedName);
    }

    // Set timeout on the hit element for subsequent attribute queries during walk-up
    AXUIElementSetMessagingTimeout(hitElement, 0.05);

    // Use window bounds from CGWindowList cache as the containing window frame.
    // This avoids an extra AX round-trip to query kAXWindowAttribute.
    const CGRect containingWindowFrame = CGRectMake(
        windowBounds.x(), windowBounds.y(),
        windowBounds.width(), windowBounds.height());

    // Walk up from the deepest element to find the best-fit element for selection.
    // The hit-test returns the most deeply nested element (could be a single text node),
    // so we look for the first ancestor with reasonable bounds.
    // Transfer ownership of hitElement into current (no extra retain needed).
    AXUIElementRef current = hitElement;
    hitElement = nullptr;

    constexpr int kMaxWalkDepth = 12;
    constexpr CGFloat kMinElementSize = 10.0;
    constexpr CGFloat kMaxWindowAreaRatio = 0.90;

    std::optional<DetectedElement> result;

    for (int depth = 0; depth < kMaxWalkDepth && current; ++depth) {
        // Stop at window/application level -- those are handled by CGWindowList
        QString role = getAXElementRole(current);
        if (role == "AXWindow" || role == "AXApplication" || role == "AXSystemWide") {
            break;
        }

        // Check if this element has usable bounds
        CGRect frame;
        bool hasUsableBounds = getAXElementFrame(current, frame) &&
            frame.size.width >= kMinElementSize &&
            frame.size.height >= kMinElementSize;

        if (hasUsableBounds) {
            // Skip elements that are nearly as large as the containing window
            const CGFloat windowArea = containingWindowFrame.size.width * containingWindowFrame.size.height;
            const CGFloat elementArea = frame.size.width * frame.size.height;
            const bool tooLarge = windowArea > 0 && (elementArea / windowArea) >= kMaxWindowAreaRatio;

            if (!tooLarge) {
                DetectedElement element;
                element.bounds = QRect(
                    static_cast<int>(frame.origin.x),
                    static_cast<int>(frame.origin.y),
                    static_cast<int>(frame.size.width),
                    static_cast<int>(frame.size.height));
                element.windowTitle = role;
                element.ownerApp = ownerApp;
                element.windowLayer = 1;
                element.windowId = 0;
                element.elementType = ElementType::Window;
                element.ownerPid = targetPid;

                result = element;
                break;
            }
        }

        // Walk up to parent
        AXUIElementRef parent = nullptr;
        AXUIElementCopyAttributeValue(current, kAXParentAttribute, (CFTypeRef *)&parent);
        CFRelease(current);
        current = parent;
    }

    if (current) {
        CFRelease(current);
    }

    // Update cache
    m_axCacheValid = result.has_value();
    if (m_axCacheValid) {
        m_axCache = *result;
        m_axCacheTimer.start();
    }

    return result;
}

std::optional<DetectedElement> WindowDetector::detectWindowAt(const QPoint &screenPos) const
{
    if (!m_enabled) {
        return std::nullopt;
    }

    // Step 1: Find the top-level window from CGWindowList cache.
    // This cache already excludes our own process windows.
    std::optional<DetectedElement> topWindow;
    {
        QMutexLocker locker(&m_cacheMutex);

        if (m_windowCache.empty()) {
            return std::nullopt;
        }

        QRect screenRect;
        if (m_currentScreen) {
            screenRect = m_currentScreen->geometry();
        }

        for (const auto &window : m_windowCache) {
            if (!screenRect.isNull() && !window.bounds.intersects(screenRect)) {
                continue;
            }
            if (window.bounds.contains(screenPos)) {
                topWindow = window;
                break;
            }
        }
    }
    // Mutex released here — AX IPC below must not hold it

    if (!topWindow.has_value()) {
        return std::nullopt;
    }

    // Step 2: Try child element detection using the window's PID.
    // AXUIElementCreateApplication(pid) scopes the hit-test to the target app,
    // bypassing our own overlay which blocks system-wide AX queries.
    if (m_detectionFlags.testFlag(DetectionFlag::ChildControls) && topWindow->ownerPid > 0) {
        auto childElement = detectChildElementAt(screenPos, topWindow->ownerPid, topWindow->bounds);
        if (childElement.has_value()) {
            return childElement;
        }
    }

    return topWindow;
}

void WindowDetector::refreshWindowListAsync()
{
    uint64_t requestId = ++m_refreshRequestId;

    m_refreshComplete = false;

    // Snapshot detection flags to avoid data race with main thread
    const DetectionFlags flags = m_detectionFlags;

    m_refreshFuture = QtConcurrent::run([this, requestId, flags]() {
        std::vector<DetectedElement> newCache;

        // Determine CGWindowList options based on detection flags
        CGWindowListOption options = kCGWindowListOptionOnScreenOnly;
        bool detectingSystemUI = flags & DetectionFlag::AllSystemUI;
        if (!detectingSystemUI) {
            options |= kCGWindowListExcludeDesktopElements;
        }

        CFArrayRef windowList = CGWindowListCopyWindowInfo(options, kCGNullWindowID);

        if (!windowList) {
            if (m_refreshRequestId.load() == requestId) {
                m_refreshComplete = true;
                QMetaObject::invokeMethod(this, [this]() {
                    emit windowListReady();
                }, Qt::QueuedConnection);
            }
            return;
        }

        CFIndex count = CFArrayGetCount(windowList);
        pid_t myPid = getpid();

        for (CFIndex i = 0; i < count; ++i) {
            CFDictionaryRef windowInfo = (CFDictionaryRef)CFArrayGetValueAtIndex(windowList, i);

            CFNumberRef pidRef = (CFNumberRef)CFDictionaryGetValue(windowInfo, kCGWindowOwnerPID);
            pid_t windowPid = 0;
            if (pidRef) {
                CFNumberGetValue(pidRef, kCFNumberIntType, &windowPid);
            }

            if (windowPid == myPid) {
                continue;
            }

            CFNumberRef layerRef = (CFNumberRef)CFDictionaryGetValue(windowInfo, kCGWindowLayer);
            int windowLayer = 0;
            if (layerRef) {
                CFNumberGetValue(layerRef, kCFNumberIntType, &windowLayer);
            }

            CFDictionaryRef boundsDict = (CFDictionaryRef)CFDictionaryGetValue(windowInfo, kCGWindowBounds);
            if (!boundsDict) {
                continue;
            }

            CGRect cgBounds;
            if (!CGRectMakeWithDictionaryRepresentation(boundsDict, &cgBounds)) {
                continue;
            }

            ElementType elementType = classifyElementType(windowLayer, windowInfo, cgBounds);

            if (!shouldIncludeElementType(elementType, flags)) {
                continue;
            }

            int minSize = getMinimumSize(elementType);
            if (cgBounds.size.width < minSize || cgBounds.size.height < minSize) {
                continue;
            }

            CFNumberRef windowIdRef = (CFNumberRef)CFDictionaryGetValue(windowInfo, kCGWindowNumber);
            uint32_t windowId = 0;
            if (windowIdRef) {
                CFNumberGetValue(windowIdRef, kCFNumberSInt32Type, &windowId);
            }

            QString windowTitle;
            CFStringRef nameRef = (CFStringRef)CFDictionaryGetValue(windowInfo, kCGWindowName);
            if (nameRef) {
                NSString *name = (__bridge NSString *)nameRef;
                windowTitle = QString::fromNSString(name);
            }

            QString ownerApp;
            CFStringRef ownerNameRef = (CFStringRef)CFDictionaryGetValue(windowInfo, kCGWindowOwnerName);
            if (ownerNameRef) {
                NSString *ownerName = (__bridge NSString *)ownerNameRef;
                ownerApp = QString::fromNSString(ownerName);
            }

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
            element.nativeHandle = static_cast<quintptr>(windowId);
            element.elementType = elementType;
            element.ownerPid = windowPid;

            newCache.push_back(element);
        }

        CFRelease(windowList);

        if (m_refreshRequestId.load() != requestId) {
            qDebug() << "WindowDetector: Discarding stale refresh result";
            return;
        }

        {
            QMutexLocker locker(&m_cacheMutex);
            m_windowCache = std::move(newCache);
        }

        m_refreshComplete = true;
        QMetaObject::invokeMethod(this, [this]() {
            emit windowListReady();
        }, Qt::QueuedConnection);
    });
}

bool WindowDetector::isRefreshComplete() const
{
    return m_refreshComplete.load();
}
