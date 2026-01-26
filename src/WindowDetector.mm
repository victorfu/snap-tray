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
    case ElementType::UIElement:
        return flags.testFlag(DetectionFlag::UIElements);
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
    case ElementType::UIElement:
        return 5;   // UI elements can be small (buttons, icons)
    case ElementType::Unknown:
        return 50;
    }
    return 50;
}

// Map AX role string to simplified role name
QString axRoleToSimplifiedRole(NSString* role)
{
    if (!role) return "element";

    NSDictionary* roleMap = @{
        NSAccessibilityButtonRole: @"button",
        NSAccessibilityCheckBoxRole: @"checkbox",
        NSAccessibilityRadioButtonRole: @"radiobutton",
        NSAccessibilityTextFieldRole: @"textfield",
        NSAccessibilityTextAreaRole: @"textarea",
        NSAccessibilityStaticTextRole: @"text",
        NSAccessibilityImageRole: @"image",
        NSAccessibilityLinkRole: @"link",
        NSAccessibilityListRole: @"list",
        NSAccessibilityTableRole: @"table",
        NSAccessibilityRowRole: @"row",
        NSAccessibilityCellRole: @"cell",
        NSAccessibilityColumnRole: @"column",
        NSAccessibilityMenuRole: @"menu",
        NSAccessibilityMenuItemRole: @"menuitem",
        NSAccessibilityMenuBarRole: @"menubar",
        NSAccessibilityToolbarRole: @"toolbar",
        NSAccessibilityGroupRole: @"group",
        NSAccessibilitySliderRole: @"slider",
        NSAccessibilityProgressIndicatorRole: @"progressbar",
        NSAccessibilityComboBoxRole: @"combobox",
        NSAccessibilityPopUpButtonRole: @"popupbutton",
        NSAccessibilityTabGroupRole: @"tablist",
        NSAccessibilityScrollAreaRole: @"scrollarea",
        NSAccessibilityScrollBarRole: @"scrollbar",
        NSAccessibilitySplitGroupRole: @"splitgroup",
        NSAccessibilityOutlineRole: @"tree",
        NSAccessibilityBrowserRole: @"browser",
        NSAccessibilityWindowRole: @"window",
        @"AXWebArea": @"webpage",
        @"AXHeading": @"heading",
        @"AXSection": @"section",
        @"AXArticle": @"article",
        @"AXBlockquote": @"blockquote",
        @"AXLandmarkMain": @"main",
        @"AXLandmarkNavigation": @"navigation",
    };

    NSString* simplified = roleMap[role];
    if (simplified) {
        return QString::fromNSString(simplified);
    }

    // Return role without AX prefix
    QString qRole = QString::fromNSString(role);
    if (qRole.startsWith("AX")) {
        qRole = qRole.mid(2).toLower();
    }
    return qRole;
}

// Get element bounds from AXUIElement
bool getElementBounds(AXUIElementRef element, QRect& outBounds)
{
    if (!element) return false;

    AXValueRef posValue = nullptr;
    AXValueRef sizeValue = nullptr;

    AXError error = AXUIElementCopyAttributeValue(element, kAXPositionAttribute, (CFTypeRef*)&posValue);
    if (error != kAXErrorSuccess || !posValue) {
        return false;
    }

    error = AXUIElementCopyAttributeValue(element, kAXSizeAttribute, (CFTypeRef*)&sizeValue);
    if (error != kAXErrorSuccess || !sizeValue) {
        CFRelease(posValue);
        return false;
    }

    CGPoint position;
    CGSize size;

    bool success = AXValueGetValue(posValue, kAXValueTypeCGPoint, &position) &&
                   AXValueGetValue(sizeValue, kAXValueTypeCGSize, &size);

    CFRelease(posValue);
    CFRelease(sizeValue);

    if (success) {
        outBounds = QRect(
            static_cast<int>(position.x),
            static_cast<int>(position.y),
            static_cast<int>(size.width),
            static_cast<int>(size.height)
        );
    }

    return success;
}

// Get element role from AXUIElement
QString getElementRole(AXUIElementRef element)
{
    if (!element) return QString();

    CFTypeRef roleRef = nullptr;
    AXError error = AXUIElementCopyAttributeValue(element, kAXRoleAttribute, &roleRef);
    if (error != kAXErrorSuccess || !roleRef) {
        return QString();
    }

    QString role;
    if (CFGetTypeID(roleRef) == CFStringGetTypeID()) {
        role = axRoleToSimplifiedRole((__bridge NSString*)roleRef);
    }
    CFRelease(roleRef);
    return role;
}

// Get element title/description from AXUIElement
QString getElementTitle(AXUIElementRef element)
{
    if (!element) return QString();

    // Try title first
    CFTypeRef valueRef = nullptr;
    AXError error = AXUIElementCopyAttributeValue(element, kAXTitleAttribute, &valueRef);

    if (error != kAXErrorSuccess || !valueRef) {
        // Try description
        error = AXUIElementCopyAttributeValue(element, kAXDescriptionAttribute, &valueRef);
    }

    if (error != kAXErrorSuccess || !valueRef) {
        // Try value (for text fields, etc.)
        error = AXUIElementCopyAttributeValue(element, kAXValueAttribute, &valueRef);
    }

    if (error == kAXErrorSuccess && valueRef) {
        QString result;
        if (CFGetTypeID(valueRef) == CFStringGetTypeID()) {
            result = QString::fromNSString((__bridge NSString*)valueRef);
        }
        CFRelease(valueRef);
        return result;
    }

    return QString();
}

// Get accessibility description
QString getElementDescription(AXUIElementRef element)
{
    if (!element) return QString();

    CFTypeRef valueRef = nullptr;
    AXError error = AXUIElementCopyAttributeValue(element, kAXHelpAttribute, &valueRef);

    if (error == kAXErrorSuccess && valueRef) {
        QString result;
        if (CFGetTypeID(valueRef) == CFStringGetTypeID()) {
            result = QString::fromNSString((__bridge NSString*)valueRef);
        }
        CFRelease(valueRef);
        return result;
    }

    return QString();
}

bool isWebContainerRole(const QString& role)
{
    return role == "webpage" || role == "document" || role == "browser";
}

bool isTextRole(const QString& role)
{
    return role == "text";
}

bool isDecorativeRole(const QString& role)
{
    static const QSet<QString> decorativeRoles = {
        "scrollbar",
        "splitter",
        "splitgroup",
        "separator",
        "thumb",
        "growarea",
    };
    return decorativeRoles.contains(role.toLower());
}

bool isWebContext(AXUIElementRef element)
{
    if (!element) {
        return false;
    }

    AXUIElementRef current = element;
    AXUIElementRef owned = nullptr;

    for (int i = 0; i < 10 && current; ++i) {
        if (isWebContainerRole(getElementRole(current))) {
            if (owned) {
                CFRelease(owned);
            }
            return true;
        }

        AXUIElementRef parent = nullptr;
        AXError error = AXUIElementCopyAttributeValue(current, kAXParentAttribute, (CFTypeRef*)&parent);
        if (owned) {
            CFRelease(owned);
            owned = nullptr;
        }
        if (error != kAXErrorSuccess || !parent) {
            break;
        }
        owned = parent;
        current = parent;
    }

    if (owned) {
        CFRelease(owned);
    }

    return false;
}

CFArrayRef copyChildElements(AXUIElementRef element)
{
    if (!element) {
        return nullptr;
    }

    CFTypeRef childrenRef = nullptr;
    AXError error = AXUIElementCopyAttributeValue(element, kAXChildrenAttribute, &childrenRef);
    if (error == kAXErrorSuccess && childrenRef &&
        CFGetTypeID(childrenRef) == CFArrayGetTypeID() &&
        CFArrayGetCount((CFArrayRef)childrenRef) > 0) {
        return (CFArrayRef)childrenRef;
    }
    if (childrenRef) {
        CFRelease(childrenRef);
    }

    childrenRef = nullptr;
    static CFStringRef kChildrenInNavigationOrderAttribute = CFSTR("AXChildrenInNavigationOrder");
    error = AXUIElementCopyAttributeValue(element, kChildrenInNavigationOrderAttribute, &childrenRef);
    if (error == kAXErrorSuccess && childrenRef &&
        CFGetTypeID(childrenRef) == CFArrayGetTypeID()) {
        return (CFArrayRef)childrenRef;
    }

    if (childrenRef) {
        CFRelease(childrenRef);
    }

    return nullptr;
}

std::optional<pid_t> getWindowOwnerPid(uint32_t windowId)
{
    if (windowId == 0) {
        return std::nullopt;
    }

    CFArrayRef windowInfoList = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, windowId);
    if (!windowInfoList) {
        return std::nullopt;
    }

    pid_t pid = 0;
    if (CFArrayGetCount(windowInfoList) > 0) {
        CFDictionaryRef windowInfo = (CFDictionaryRef)CFArrayGetValueAtIndex(windowInfoList, 0);
        CFNumberRef pidRef = (CFNumberRef)CFDictionaryGetValue(windowInfo, kCGWindowOwnerPID);
        if (pidRef) {
            CFNumberGetValue(pidRef, kCFNumberIntType, &pid);
        }
    }

    CFRelease(windowInfoList);

    if (pid == 0) {
        return std::nullopt;
    }

    return pid;
}

// Find the smallest element at a given point by traversing the accessibility tree
// Returns nullptr if no suitable element found, otherwise returns a retained element
AXUIElementRef findSmallestElementAtPoint(AXUIElementRef parent,
                                          CGPoint point,
                                          QRect& bestBounds,
                                          int depth,
                                          int minSize,
                                          bool skipText)
{
    // Limit recursion depth to prevent stack overflow
    if (!parent || depth > 20) return nullptr;

    // Check children
    CFArrayRef children = copyChildElements(parent);
    if (!children) {
        return nullptr;
    }

    AXUIElementRef smallestElement = nullptr;
    int smallestArea = INT_MAX;
    const int minArea = minSize * minSize;

    CFIndex count = CFArrayGetCount(children);
    for (CFIndex i = 0; i < count; i++) {
        AXUIElementRef child = (AXUIElementRef)CFArrayGetValueAtIndex(children, i);
        if (!child) continue;

        QRect childBounds;
        if (!getElementBounds(child, childBounds)) continue;

        // Skip elements that are too small or too large
        if (childBounds.width() < minSize || childBounds.height() < minSize) continue;
        if (childBounds.width() > 10000 || childBounds.height() > 10000) continue;

        // Check if point is within bounds
        if (childBounds.contains(QPoint(static_cast<int>(point.x), static_cast<int>(point.y)))) {
            // Recursively check children for even smaller elements
            QRect deeperBounds;
            AXUIElementRef deeperElement = findSmallestElementAtPoint(
                child, point, deeperBounds, depth + 1, minSize, skipText);
            if (deeperElement) {
                int deeperArea = deeperBounds.width() * deeperBounds.height();
                if (deeperArea >= minArea && deeperArea < smallestArea) {
                    if (smallestElement) {
                        CFRelease(smallestElement);
                    }
                    smallestArea = deeperArea;
                    smallestElement = deeperElement;
                    bestBounds = deeperBounds;
                } else {
                    CFRelease(deeperElement);
                }
            }

            if (skipText && isTextRole(getElementRole(child))) {
                continue;
            }

            int area = childBounds.width() * childBounds.height();
            if (area >= minArea && area < smallestArea) {
                // Release previous smallest if we had one
                if (smallestElement) {
                    CFRelease(smallestElement);
                }
                smallestArea = area;
                smallestElement = child;
                CFRetain(smallestElement);
                bestBounds = childBounds;
            }
        }
    }

    CFRelease(children);
    return smallestElement;
}

AXUIElementRef promoteElementAtPoint(AXUIElementRef element,
                                     int minSize,
                                     bool skipText,
                                     QRect& outBounds,
                                     QString& outRole)
{
    if (!element) {
        return nullptr;
    }

    AXUIElementRef current = element;
    AXUIElementRef owned = nullptr;

    for (int i = 0; i < 6 && current; ++i) {
        QRect bounds;
        if (getElementBounds(current, bounds)) {
            QString role = getElementRole(current);
            bool isText = skipText && isTextRole(role);
            bool tooSmall = bounds.width() < minSize || bounds.height() < minSize;
            if (!isText && !tooSmall) {
                outBounds = bounds;
                outRole = role;
                if (current == element) {
                    CFRetain(current);
                } else {
                    owned = nullptr;
                }
                if (owned) {
                    CFRelease(owned);
                }
                return current;
            }
        }

        AXUIElementRef parent = nullptr;
        AXError error = AXUIElementCopyAttributeValue(current, kAXParentAttribute, (CFTypeRef*)&parent);
        if (owned) {
            CFRelease(owned);
            owned = nullptr;
        }
        if (error != kAXErrorSuccess || !parent) {
            break;
        }
        owned = parent;
        current = parent;
    }

    if (owned) {
        CFRelease(owned);
    }

    return nullptr;
}

} // anonymous namespace

WindowDetector::WindowDetector(QObject *parent)
    : QObject(parent)
    , m_currentScreen(nullptr)
    , m_enabled(true)
    , m_uiElementDetectionEnabled(true)
    , m_detectionFlags(DetectionFlag::All | DetectionFlag::UIElements)
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

void WindowDetector::requestAccessibilityPermission()
{
    // Request accessibility permission with system prompt
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
        element.elementType = elementType;

        m_windowCache.push_back(element);
    }

    CFRelease(windowList);
}

std::optional<DetectedElement> WindowDetector::detectWindowAt(const QPoint &screenPos) const
{
    // Lock mutex for thread-safe cache access (matches Windows implementation)
    QMutexLocker locker(&m_cacheMutex);

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
                QMetaObject::invokeMethod(this, "windowListReady", Qt::QueuedConnection);
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
            element.elementType = elementType;

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
        QMetaObject::invokeMethod(this, "windowListReady", Qt::QueuedConnection);
    });
}

bool WindowDetector::isRefreshComplete() const
{
    return m_refreshComplete.load();
}

std::optional<DetectedElement> WindowDetector::detectUIElementAt(const QPoint &screenPos) const
{
    qDebug() << "detectUIElementAt: called at" << screenPos;

    if (!m_enabled || !m_uiElementDetectionEnabled) {
        qDebug() << "detectUIElementAt: DISABLED (enabled=" << m_enabled
                 << ", uiElementEnabled=" << m_uiElementDetectionEnabled << ")";
        return std::nullopt;
    }

    if (!m_detectionFlags.testFlag(DetectionFlag::UIElements)) {
        qDebug() << "detectUIElementAt: UIElements flag NOT SET in detectionFlags";
        return std::nullopt;
    }

    // Check accessibility permission
    if (!hasAccessibilityPermission()) {
        qDebug() << "detectUIElementAt: NO ACCESSIBILITY PERMISSION";
        return std::nullopt;
    }

    qDebug() << "detectUIElementAt: all checks passed, calling AX API...";

    @autoreleasepool {
        const int kMinUiElementSize = 12;
        const int kMinWebElementSize = 24;

        // Get the system-wide accessibility element
        AXUIElementRef systemWide = AXUIElementCreateSystemWide();
        if (!systemWide) {
            return std::nullopt;
        }

        CGPoint cgPoint = CGPointMake(screenPos.x(), screenPos.y());

        // Get element at point
        AXUIElementRef elementAtPoint = nullptr;
        AXError error = AXUIElementCopyElementAtPosition(systemWide, cgPoint.x, cgPoint.y, &elementAtPoint);
        CFRelease(systemWide);

        if (error != kAXErrorSuccess || !elementAtPoint) {
            qDebug() << "detectUIElementAt: AX API failed, error=" << error;
            return std::nullopt;
        }

        // Check if element belongs to our own process - skip if so
        // This avoids detecting our own overlay window instead of the content underneath
        pid_t elementPid = 0;
        AXUIElementGetPid(elementAtPoint, &elementPid);
        pid_t myPid = [[NSProcessInfo processInfo] processIdentifier];
        if (elementPid == myPid) {
            qDebug() << "detectUIElementAt: element belongs to our own process, trying underlying window";
            auto windowElement = detectWindowAt(screenPos);
            if (!windowElement.has_value()) {
                CFRelease(elementAtPoint);
                return std::nullopt;
            }

            auto targetPid = getWindowOwnerPid(windowElement->windowId);
            if (!targetPid.has_value() || targetPid.value() == myPid) {
                CFRelease(elementAtPoint);
                return std::nullopt;
            }

            AXUIElementRef appElement = AXUIElementCreateApplication(targetPid.value());
            if (!appElement) {
                CFRelease(elementAtPoint);
                return std::nullopt;
            }

            QRect fallbackBounds;
            AXUIElementRef fallbackElement = findSmallestElementAtPoint(
                appElement, cgPoint, fallbackBounds, 0, kMinUiElementSize, false);
            CFRelease(appElement);

            if (!fallbackElement) {
                CFRelease(elementAtPoint);
                return std::nullopt;
            }

            CFRelease(elementAtPoint);
            elementAtPoint = fallbackElement;
        }

        const bool isWeb = isWebContext(elementAtPoint);
        const bool skipText = isWeb;
        const int minElementSize = isWeb ? kMinWebElementSize : kMinUiElementSize;

        // Get element bounds
        QRect elementBounds;
        if (!getElementBounds(elementAtPoint, elementBounds)) {
            qDebug() << "detectUIElementAt: getElementBounds failed";
            CFRelease(elementAtPoint);
            return std::nullopt;
        }

        qDebug() << "detectUIElementAt: initial bounds=" << elementBounds;

        // Get the role
        QString role = getElementRole(elementAtPoint);
        qDebug() << "detectUIElementAt: initial role=" << role;

        const bool needsPromotion =
            (skipText && isTextRole(role)) ||
            elementBounds.width() < minElementSize ||
            elementBounds.height() < minElementSize;
        if (needsPromotion) {
            QRect promotedBounds;
            QString promotedRole;
            AXUIElementRef promoted = promoteElementAtPoint(
                elementAtPoint, minElementSize, skipText, promotedBounds, promotedRole);
            if (!promoted) {
                CFRelease(elementAtPoint);
                return std::nullopt;
            }
            CFRelease(elementAtPoint);
            elementAtPoint = promoted;
            elementBounds = promotedBounds;
            role = promotedRole;
        }

        const int kChildSearchMinSize = minElementSize;
        if (elementBounds.width() > kChildSearchMinSize || elementBounds.height() > kChildSearchMinSize) {
            QRect smallerBounds;
            AXUIElementRef smallerElement = findSmallestElementAtPoint(
                elementAtPoint, cgPoint, smallerBounds, 0, minElementSize, skipText);
            if (smallerElement) {
                // Verify the child is actually smaller
                qint64 originalArea = static_cast<qint64>(elementBounds.width()) * elementBounds.height();
                qint64 smallerArea = static_cast<qint64>(smallerBounds.width()) * smallerBounds.height();
                if (smallerArea < originalArea) {
                    CFRelease(elementAtPoint);
                    elementAtPoint = smallerElement;
                    elementBounds = smallerBounds;
                    role = getElementRole(elementAtPoint);
                } else {
                    CFRelease(smallerElement);
                }
            }
        }

        // Final size check
        if (elementBounds.width() < minElementSize || elementBounds.height() < minElementSize) {
            CFRelease(elementAtPoint);
            return std::nullopt;
        }

        // Get element title and description
        QString title = getElementTitle(elementAtPoint);
        QString description = getElementDescription(elementAtPoint);

        CFRelease(elementAtPoint);

        // Create detected element
        DetectedElement element;
        element.bounds = elementBounds;
        element.windowTitle = title;
        element.ownerApp = QString();
        element.windowLayer = 1;
        element.windowId = 0;
        element.elementType = ElementType::UIElement;
        element.role = role;
        element.description = description;

        qDebug() << "detectUIElementAt: SUCCESS - returning element"
                 << "bounds=" << element.bounds
                 << "role=" << element.role
                 << "title=" << element.windowTitle;

        return element;
    }
}

void WindowDetector::setUIElementDetectionEnabled(bool enabled)
{
    m_uiElementDetectionEnabled = enabled;
}

bool WindowDetector::isUIElementDetectionEnabled() const
{
    return m_uiElementDetectionEnabled;
}

void WindowDetector::setDetectionFlags(DetectionFlags flags)
{
    m_detectionFlags = flags;
}
