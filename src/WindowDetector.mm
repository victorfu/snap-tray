#include "WindowDetector.h"
#include "WindowDetectorMacFilters.h"

#include <QImage>
#include <QScreen>
#include <QDebug>
#include <QByteArray>
#include <QtConcurrent>
#include <unistd.h>

#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 120300
#define HAS_SCREENCAPTUREKIT 1
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#else
#define HAS_SCREENCAPTUREKIT 0
#endif

namespace {

const CFStringRef kAXVisibleAttributeCompat = CFSTR("AXVisible");

DetectionFlags flagsForQueryMode(DetectionFlags baseFlags, WindowDetector::QueryMode queryMode)
{
    if (queryMode == WindowDetector::QueryMode::TopLevelOnly) {
        baseFlags &= ~DetectionFlags(DetectionFlag::ChildControls);
    }
    return baseFlags;
}

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

QImage createQImageFromCGImage(CGImageRef sourceImage)
{
    if (!sourceImage) {
        return QImage();
    }

    const size_t width = CGImageGetWidth(sourceImage);
    const size_t height = CGImageGetHeight(sourceImage);
    if (width == 0 || height == 0) {
        return QImage();
    }

    QImage target(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGBA8888_Premultiplied);
    if (target.isNull()) {
        return QImage();
    }

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    if (!colorSpace) {
        return QImage();
    }

    CGContextRef context = CGBitmapContextCreate(
        target.bits(),
        width,
        height,
        8,
        static_cast<size_t>(target.bytesPerLine()),
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colorSpace);

    if (!context) {
        return QImage();
    }

    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextDrawImage(context, CGRectMake(0.0, 0.0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)), sourceImage);
    CGContextRelease(context);

    return target;
}

QRect nonTransparentImageBounds(const QImage &image, int alphaThreshold = 8)
{
    if (image.isNull()) {
        return QRect();
    }

    int minX = image.width();
    int minY = image.height();
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < image.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(row[x]) > alphaThreshold) {
                minX = qMin(minX, x);
                minY = qMin(minY, y);
                maxX = qMax(maxX, x);
                maxY = qMax(maxY, y);
            }
        }
    }

    if (maxX < minX || maxY < minY) {
        return QRect();
    }

    return QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

bool shouldRefinePopupBoundsFromImage(ElementType elementType, const QRect &bounds)
{
    if (elementType != ElementType::ContextMenu &&
        elementType != ElementType::PopupMenu &&
        elementType != ElementType::StatusBarItem) {
        return false;
    }

    // Small menu bar items already have reasonable bounds. The problematic
    // Control Center host windows are tall, oversized containers.
    return bounds.width() >= 120 && bounds.height() >= 200;
}

bool shouldPreferTopLevelBoundsForElementType(ElementType elementType)
{
    switch (elementType) {
    case ElementType::ContextMenu:
    case ElementType::PopupMenu:
    case ElementType::StatusBarItem:
        return true;
    case ElementType::Window:
    case ElementType::Dialog:
    case ElementType::Unknown:
        return false;
    }

    return false;
}

QImage captureWindowImage(CGWindowID windowId)
{
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 14.0, *)) {
        dispatch_semaphore_t contentSemaphore = dispatch_semaphore_create(0);
        __block SCShareableContent *shareableContent = nil;
        __block NSError *shareableContentError = nil;

        [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                                   onScreenWindowsOnly:YES
                                                     completionHandler:^(
            SCShareableContent *content, NSError *error) {
            shareableContent = [content retain];
            shareableContentError = [error retain];
            dispatch_semaphore_signal(contentSemaphore);
        }];

        const long contentWaitResult = dispatch_semaphore_wait(
            contentSemaphore,
            dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));
        if (contentWaitResult != 0 || !shareableContent || shareableContentError) {
            [shareableContent release];
            [shareableContentError release];
            return QImage();
        }

        SCWindow *targetWindow = nil;
        for (SCWindow *window in shareableContent.windows) {
            if (window.windowID == windowId) {
                targetWindow = [window retain];
                break;
            }
        }

        [shareableContent release];
        [shareableContentError release];

        if (!targetWindow) {
            return QImage();
        }

        SCContentFilter *filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:targetWindow];
        SCShareableContentInfo *filterInfo = [SCShareableContent infoForFilter:filter];
        const CGRect contentRect = filterInfo ? filterInfo.contentRect : targetWindow.frame;
        const qreal pointPixelScale = filterInfo ? static_cast<qreal>(filterInfo.pointPixelScale) : 1.0;

        SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
        config.width = static_cast<size_t>(qMax(1, qRound(CGRectGetWidth(contentRect) * pointPixelScale)));
        config.height = static_cast<size_t>(qMax(1, qRound(CGRectGetHeight(contentRect) * pointPixelScale)));
        config.showsCursor = NO;
        config.ignoreShadowsSingleWindow = YES;
        if (@available(macOS 14.2, *)) {
            config.includeChildWindows = YES;
        }

        dispatch_semaphore_t captureSemaphore = dispatch_semaphore_create(0);
        __block CGImageRef capturedImage = nil;
        __block NSError *captureError = nil;

        [SCScreenshotManager captureImageWithFilter:filter
                                      configuration:config
                                  completionHandler:^(
            CGImageRef image, NSError *error) {
            if (image) {
                capturedImage = CGImageRetain(image);
            }
            captureError = [error retain];
            dispatch_semaphore_signal(captureSemaphore);
        }];

        [config release];
        [filter release];
        [targetWindow release];

        const long captureWaitResult = dispatch_semaphore_wait(
            captureSemaphore,
            dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));
        if (captureWaitResult != 0 || !capturedImage || captureError) {
            if (capturedImage) {
                CGImageRelease(capturedImage);
            }
            [captureError release];
            return QImage();
        }

        const QImage image = createQImageFromCGImage(capturedImage);
        CGImageRelease(capturedImage);
        [captureError release];
        return image;
    }
#endif

    return QImage();
}

QRect refineVisibleBoundsFromWindowImage(CGWindowID windowId, const QRect &rawBounds)
{
    if (windowId == 0 || !rawBounds.isValid() || rawBounds.isEmpty()) {
        return QRect();
    }

    const QImage image = captureWindowImage(windowId);
    if (image.isNull()) {
        return QRect();
    }

    const QRect imageBounds = nonTransparentImageBounds(image);
    if (!imageBounds.isValid() || imageBounds.isEmpty()) {
        return QRect();
    }

    const qreal scaleX = rawBounds.width() > 0
        ? static_cast<qreal>(rawBounds.width()) / static_cast<qreal>(image.width())
        : 1.0;
    const qreal scaleY = rawBounds.height() > 0
        ? static_cast<qreal>(rawBounds.height()) / static_cast<qreal>(image.height())
        : 1.0;

    const int refinedX = rawBounds.x() + qRound(imageBounds.x() * scaleX);
    const int refinedY = rawBounds.y() + qRound(imageBounds.y() * scaleY);
    const int refinedWidth = qMax(1, qRound(imageBounds.width() * scaleX));
    const int refinedHeight = qMax(1, qRound(imageBounds.height() * scaleY));

    return QRect(refinedX, refinedY, refinedWidth, refinedHeight);
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

template <typename T>
bool readCFNumberValue(CFDictionaryRef dictionary, const void *key, CFNumberType numberType, T &value)
{
    if (!dictionary || !key) {
        return false;
    }
    CFNumberRef numberRef = static_cast<CFNumberRef>(CFDictionaryGetValue(dictionary, key));
    if (!numberRef) {
        return false;
    }
    return CFNumberGetValue(numberRef, numberType, &value);
}

std::optional<bool> readCFBooleanValue(CFDictionaryRef dictionary, const void *key)
{
    if (!dictionary || !key) {
        return std::nullopt;
    }
    CFTypeRef valueRef = CFDictionaryGetValue(dictionary, key);
    if (!valueRef || CFGetTypeID(valueRef) != CFBooleanGetTypeID()) {
        return std::nullopt;
    }
    return CFBooleanGetValue(static_cast<CFBooleanRef>(valueRef));
}

std::optional<double> readWindowAlpha(CFDictionaryRef windowInfo)
{
    double alpha = 0.0;
    if (readCFNumberValue(windowInfo, kCGWindowAlpha, kCFNumberDoubleType, alpha)) {
        return alpha;
    }

    float alphaFloat = 0.0f;
    if (readCFNumberValue(windowInfo, kCGWindowAlpha, kCFNumberFloatType, alphaFloat)) {
        return static_cast<double>(alphaFloat);
    }

    return std::nullopt;
}

QString readWindowString(CFDictionaryRef windowInfo, const void *key)
{
    if (!windowInfo || !key) {
        return QString();
    }
    CFStringRef strRef = static_cast<CFStringRef>(CFDictionaryGetValue(windowInfo, key));
    if (!strRef) {
        return QString();
    }
    return QString::fromCFString(strRef);
}

std::optional<bool> copyAXBooleanAttribute(AXUIElementRef element, CFStringRef attribute)
{
    if (!element || !attribute) {
        return std::nullopt;
    }

    CFTypeRef valueRef = nullptr;
    AXError err = AXUIElementCopyAttributeValue(element, attribute, &valueRef);
    if (err != kAXErrorSuccess || !valueRef) {
        return std::nullopt;
    }

    std::optional<bool> result;
    if (CFGetTypeID(valueRef) == CFBooleanGetTypeID()) {
        result = CFBooleanGetValue(static_cast<CFBooleanRef>(valueRef));
    } else if (CFGetTypeID(valueRef) == CFNumberGetTypeID()) {
        int intValue = 0;
        if (CFNumberGetValue(static_cast<CFNumberRef>(valueRef), kCFNumberIntType, &intValue)) {
            result = intValue != 0;
        }
    }

    CFRelease(valueRef);
    return result;
}

std::optional<DetectedElement> buildDetectedTopLevelElement(
    CFDictionaryRef windowInfo, pid_t ownPid, DetectionFlags detectionFlags)
{
    if (!windowInfo) {
        return std::nullopt;
    }

    pid_t windowPid = 0;
    readCFNumberValue(windowInfo, kCGWindowOwnerPID, kCFNumberIntType, windowPid);
    if (windowPid == ownPid) {
        return std::nullopt;
    }

    int windowLayer = 0;
    readCFNumberValue(windowInfo, kCGWindowLayer, kCFNumberIntType, windowLayer);

    CFDictionaryRef boundsDict = static_cast<CFDictionaryRef>(CFDictionaryGetValue(windowInfo, kCGWindowBounds));
    if (!boundsDict) {
        return std::nullopt;
    }

    CGRect cgBounds;
    if (!CGRectMakeWithDictionaryRepresentation(boundsDict, &cgBounds)) {
        return std::nullopt;
    }

    QRect bounds(
        static_cast<int>(cgBounds.origin.x),
        static_cast<int>(cgBounds.origin.y),
        static_cast<int>(cgBounds.size.width),
        static_cast<int>(cgBounds.size.height));

    const std::optional<double> alpha = readWindowAlpha(windowInfo);
    if (WindowDetectorMacFilters::shouldRejectTopLevelByAlpha(alpha)) {
        return std::nullopt;
    }

    const std::optional<bool> isOnscreen = readCFBooleanValue(windowInfo, kCGWindowIsOnscreen);
    if (WindowDetectorMacFilters::shouldRejectTopLevelByOnscreen(isOnscreen)) {
        return std::nullopt;
    }

    const ElementType elementType = classifyElementType(windowLayer, windowInfo, cgBounds);
    if (!shouldIncludeElementType(elementType, detectionFlags)) {
        return std::nullopt;
    }

    const int minSize = getMinimumSize(elementType);
    if (cgBounds.size.width < minSize || cgBounds.size.height < minSize) {
        return std::nullopt;
    }

    int windowId = 0;
    readCFNumberValue(windowInfo, kCGWindowNumber, kCFNumberIntType, windowId);

    if (windowId > 0 && shouldRefinePopupBoundsFromImage(elementType, bounds)) {
        const QRect refinedBounds = refineVisibleBoundsFromWindowImage(static_cast<CGWindowID>(windowId), bounds);
        if (refinedBounds.isValid() && !refinedBounds.isEmpty()) {
            bounds = refinedBounds;
        }
    }

    DetectedElement element;
    element.bounds = bounds;
    element.windowTitle = readWindowString(windowInfo, kCGWindowName);
    element.ownerApp = readWindowString(windowInfo, kCGWindowOwnerName);
    element.windowLayer = windowLayer;
    element.windowId = static_cast<uint32_t>(windowId);
    element.elementType = elementType;
    element.ownerPid = windowPid;
    return element;
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

bool WindowDetector::hasAccessibilityPermission(bool promptIfMissing)
{
    // Check accessibility trust state. We only request prompt when specifically asked.
    NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @(promptIfMissing)};
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

void WindowDetector::refreshWindowList(QueryMode queryMode)
{
    ++m_refreshRequestId;
    m_refreshComplete = false;
    const DetectionFlags flags = flagsForQueryMode(m_detectionFlags, queryMode);

    std::vector<DetectedElement> previousCache;
    QScreen* previousScreen = nullptr;
    QueryMode previousQueryMode = QueryMode::TopLevelOnly;

    {
        QMutexLocker locker(&m_cacheMutex);
        previousCache = m_windowCache;
        previousScreen = m_cacheScreen;
        previousQueryMode = m_cacheQueryMode;
        m_windowCache.clear();
        m_cacheReady = false;
        m_cacheScreen = nullptr;
    }

    if (!m_enabled) {
        QMutexLocker locker(&m_cacheMutex);
        m_cacheScreen = m_currentScreen.data();
        m_cacheQueryMode = queryMode;
        m_cacheReady = true;
        m_refreshComplete = true;
        return;
    }

    // Determine CGWindowList options based on detection flags
    CGWindowListOption options = kCGWindowListOptionOnScreenOnly;
    bool detectingSystemUI = flags & DetectionFlag::AllSystemUI;
    if (!detectingSystemUI) {
        options |= kCGWindowListExcludeDesktopElements;
    }

    CFArrayRef windowList = CGWindowListCopyWindowInfo(options, kCGNullWindowID);

    if (!windowList) {
        qWarning() << "WindowDetector: Failed to get window list";
        QMutexLocker locker(&m_cacheMutex);
        m_cacheScreen = m_currentScreen.data();
        m_cacheQueryMode = queryMode;
        m_cacheReady = true;
        m_refreshComplete = true;
        return;
    }

    const CFIndex count = CFArrayGetCount(windowList);
    const pid_t myPid = [[NSProcessInfo processInfo] processIdentifier];
    std::vector<DetectedElement> newCache;

    for (CFIndex i = 0; i < count; ++i) {
        CFDictionaryRef windowInfo = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windowList, i));
        if (auto element = buildDetectedTopLevelElement(windowInfo, myPid, flags)) {
            newCache.push_back(*element);
        }
    }

    CFRelease(windowList);
    mergePreservedTopLevelElements(
        newCache,
        previousCache,
        previousQueryMode,
        previousScreen,
        queryMode,
        m_currentScreen.data());

    {
        QMutexLocker locker(&m_cacheMutex);
        m_windowCache = std::move(newCache);
        m_cacheScreen = m_currentScreen.data();
        m_cacheQueryMode = queryMode;
        m_cacheReady = true;
    }

    m_refreshComplete = true;
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
        CFDictionaryRef windowInfo = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windowList, i));
        if (auto element = buildDetectedTopLevelElement(windowInfo, myPid, m_detectionFlags)) {
            m_windowCache.push_back(*element);
        }
    }

    CFRelease(windowList);
}

std::optional<DetectedElement> WindowDetector::detectChildElementAt(
    const QPoint &screenPos, qint64 targetPid, const QRect &windowBounds) const
{
    if (!hasAccessibilityPermission(false)) {
        // Trigger the system permission prompt once per app run when child control
        // detection is first attempted without accessibility trust.
        if (!m_accessibilityPromptRequested) {
            m_accessibilityPromptRequested = true;
            hasAccessibilityPermission(true);
        }
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

    struct AxCandidate {
        DetectedElement element;
        WindowDetectorMacFilters::AxCandidateTier tier = WindowDetectorMacFilters::AxCandidateTier::None;
        double area = 0.0;
        int depth = 0;
    };

    std::optional<AxCandidate> bestCandidate;
    std::optional<DetectedElement> menuItemFallback;

    for (int depth = 0; depth < kMaxWalkDepth && current; ++depth) {
        // Stop at window/application level -- those are handled by CGWindowList
        QString role = getAXElementRole(current);
        if (role == "AXWindow" || role == "AXApplication" || role == "AXSystemWide") {
            break;
        }

        const std::optional<bool> isHiddenAttr = copyAXBooleanAttribute(current, kAXHiddenAttribute);
        const std::optional<bool> isVisibleAttr = copyAXBooleanAttribute(current, kAXVisibleAttributeCompat);
        if (WindowDetectorMacFilters::shouldRejectAxByVisibility(isHiddenAttr.value_or(false), isVisibleAttr)) {
        } else {
            // Check if this element has usable bounds
            CGRect frame;
            bool hasUsableBounds = getAXElementFrame(current, frame) &&
                frame.size.width >= kMinElementSize &&
                frame.size.height >= kMinElementSize;

            if (hasUsableBounds) {
                const QRect elementBounds(
                    static_cast<int>(frame.origin.x),
                    static_cast<int>(frame.origin.y),
                    static_cast<int>(frame.size.width),
                    static_cast<int>(frame.size.height));
                const CGFloat windowArea = containingWindowFrame.size.width * containingWindowFrame.size.height;
                const CGFloat elementArea = frame.size.width * frame.size.height;
                const bool areaAllowed = WindowDetectorMacFilters::isAreaRatioAllowed(role, elementArea, windowArea);
                const bool isMenuContainerRole = WindowDetectorMacFilters::isMenuContainerRole(role);
                const bool isMenuItemRole = WindowDetectorMacFilters::isMenuItemRole(role);
                const auto candidateTier = WindowDetectorMacFilters::candidateTierForRole(role);
                const bool boundsAccepted = WindowDetectorMacFilters::shouldAcceptCandidateBounds(
                    windowBounds,
                    elementBounds,
                    screenPos,
                    static_cast<int>(kMinElementSize));

                if (areaAllowed && isMenuContainerRole) {
                    DetectedElement element;
                    element.bounds = elementBounds;
                    element.windowTitle = role;
                    element.ownerApp = ownerApp;
                    element.windowLayer = 1;
                    element.windowId = 0;
                    element.elementType = ElementType::Window;
                    element.ownerPid = targetPid;

                    m_axCacheValid = true;
                    m_axCache = element;
                    m_axCacheTimer.start();
                    if (current) {
                        CFRelease(current);
                    }
                    return element;
                }

                if (areaAllowed && isMenuItemRole) {
                    DetectedElement element;
                    element.bounds = elementBounds;
                    element.windowTitle = role;
                    element.ownerApp = ownerApp;
                    element.windowLayer = 1;
                    element.windowId = 0;
                    element.elementType = ElementType::Window;
                    element.ownerPid = targetPid;
                    menuItemFallback = element;

                    // Keep walking to find the enclosing AXMenu so menu hit-testing
                    // prefers the full menu bounds over the hovered row.
                    AXUIElementRef parent = nullptr;
                    AXUIElementCopyAttributeValue(current, kAXParentAttribute, (CFTypeRef *)&parent);
                    CFRelease(current);
                    current = parent;
                    continue;
                }

                if (areaAllowed && boundsAccepted &&
                    candidateTier != WindowDetectorMacFilters::AxCandidateTier::None) {
                    DetectedElement element;
                    element.bounds = elementBounds;
                    element.windowTitle = role;
                    element.ownerApp = ownerApp;
                    element.windowLayer = 1;
                    element.windowId = 0;
                    element.elementType = ElementType::Window;
                    element.ownerPid = targetPid;

                    if (!bestCandidate.has_value() ||
                        WindowDetectorMacFilters::shouldPreferAxCandidate(
                            candidateTier,
                            static_cast<double>(elementArea),
                            depth,
                            bestCandidate->tier,
                            bestCandidate->area,
                            bestCandidate->depth)) {
                        AxCandidate candidate;
                        candidate.element = element;
                        candidate.tier = candidateTier;
                        candidate.area = static_cast<double>(elementArea);
                        candidate.depth = depth;
                        bestCandidate = candidate;
                    }
                }

                if (windowArea > 0.0) {
                }
            }
        }

        // Walk up to parent
        AXUIElementRef parent = nullptr;
        AXUIElementCopyAttributeValue(current, kAXParentAttribute, (CFTypeRef *)&parent);
        CFRelease(current);
        current = parent;
    }

    std::optional<DetectedElement> result;
    if (bestCandidate.has_value()) {
        result = bestCandidate->element;
    } else if (menuItemFallback.has_value()) {
        result = menuItemFallback;
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

std::optional<DetectedElement> WindowDetector::detectChildElementAt(
    const QPoint &screenPos,
    const DetectedElement &topWindow,
    size_t topLevelIndex) const
{
    Q_UNUSED(topLevelIndex);
    return detectChildElementAt(screenPos, topWindow.ownerPid, topWindow.bounds);
}

std::optional<DetectedElement> WindowDetector::detectWindowAt(
    const QPoint &screenPos,
    QueryMode queryMode) const
{
    if (!m_enabled) {
        return std::nullopt;
    }

    // Step 1: Find the best top-level window from CGWindowList cache.
    // This cache already excludes our own process windows.
    //
    // We do not stop at the first bounds match because macOS menu bar popups
    // and other transient system UI can overlap an app window while living on
    // a higher CoreGraphics layer. Prefer the highest-layer match so detached
    // popups win over the underlying document window even if the raw list order
    // is not ideal for our use case.
    std::optional<DetectedElement> topWindow;
    {
        QMutexLocker locker(&m_cacheMutex);

        if (!m_cacheReady || m_cacheScreen != m_currentScreen.data()) {
            return std::nullopt;
        }

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
                if (!topWindow.has_value() || window.windowLayer > topWindow->windowLayer) {
                    topWindow = window;
                }
            }
        }
    }
    // Mutex released here — AX IPC below must not hold it

    if (!topWindow.has_value()) {
        return std::nullopt;
    }

    // For transient menu-like surfaces, the top-level CGWindow bounds represent
    // the full menu better than AX child hit-testing, which often resolves to a
    // single row or cell under the cursor.
    if (shouldPreferTopLevelBoundsForElementType(topWindow->elementType)) {
        return topWindow;
    }

    // Step 2: Try child element detection using the window's PID.
    // AXUIElementCreateApplication(pid) scopes the hit-test to the target app,
    // bypassing our own overlay which blocks system-wide AX queries.
    if (queryMode == QueryMode::IncludeChildControls &&
        m_cacheQueryMode == QueryMode::IncludeChildControls &&
        m_detectionFlags.testFlag(DetectionFlag::ChildControls)) {
        if (topWindow->ownerPid > 0) {
            auto childElement = detectChildElementAt(screenPos, *topWindow, 0);
            if (childElement.has_value()) {
                return childElement;
            }
        }
    }

    return topWindow;
}

void WindowDetector::refreshWindowListAsync(QueryMode queryMode)
{
    uint64_t requestId = ++m_refreshRequestId;
    QScreen* targetScreen = m_currentScreen.data();

    m_refreshComplete = false;

    // Snapshot detection flags to avoid data race with main thread
    const DetectionFlags flags = flagsForQueryMode(m_detectionFlags, queryMode);
    std::vector<DetectedElement> previousCache;
    QScreen* previousScreen = nullptr;
    QueryMode previousQueryMode = QueryMode::TopLevelOnly;

    {
        QMutexLocker locker(&m_cacheMutex);
        previousCache = m_windowCache;
        previousScreen = m_cacheScreen;
        previousQueryMode = m_cacheQueryMode;
        if (m_cacheScreen != targetScreen) {
            m_cacheReady = false;
        }
    }

    m_refreshFuture = QtConcurrent::run([
        this,
        requestId,
        flags,
        targetScreen,
        queryMode,
        previousCache = std::move(previousCache),
        previousScreen,
        previousQueryMode
    ]() mutable {
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
            CFDictionaryRef windowInfo = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windowList, i));
            if (auto element = buildDetectedTopLevelElement(windowInfo, myPid, flags)) {
                newCache.push_back(*element);
            }
        }

        CFRelease(windowList);
        // Region capture uses a frozen pre-capture screenshot, so when we upgrade
        // from a top-level-only snapshot to child-control queries we must keep any
        // top-level windows that vanished after our overlay activated.
        mergePreservedTopLevelElements(
            newCache,
            previousCache,
            previousQueryMode,
            previousScreen,
            queryMode,
            targetScreen);

        if (m_refreshRequestId.load() != requestId) {
            qDebug() << "WindowDetector: Discarding stale refresh result";
            return;
        }

        {
            QMutexLocker locker(&m_cacheMutex);
            m_windowCache = std::move(newCache);
            m_cacheScreen = targetScreen;
            m_cacheQueryMode = queryMode;
            m_cacheReady = true;
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

bool WindowDetector::isWindowCacheReady(QueryMode queryMode) const
{
    QMutexLocker locker(&m_cacheMutex);
    return m_cacheReady &&
           m_cacheScreen == m_currentScreen.data() &&
           static_cast<int>(m_cacheQueryMode) >= static_cast<int>(queryMode);
}
