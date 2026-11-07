// Scroll area detection (Accessibility API) and scroll injection (CGEvent)
// for macOS. Compiled with ARC; Core Foundation objects are released by hand.
#include "scrollcapture/ScrollAreaLocator.h"
#include "scrollcapture/ScrollInjector.h"

#include "platform/MacAXElement.h"
#include "scrollcapture/ScrollTargetPoints.h"

#include <QJsonArray>
#include <QThread>

#include <algorithm>
#include <limits>
#include <optional>
#include <unistd.h>
#include <utility>
#include <vector>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

namespace snaptray::scrollcapture {

namespace {

using snaptray::platform::mac::copyAXElementAttribute;
using snaptray::platform::mac::copyAXNumberAttribute;
using snaptray::platform::mac::getAXElementFrame;
using snaptray::platform::mac::getAXElementRole;

constexpr float kAXMessagingTimeoutSeconds = 0.1f;
constexpr int kMaxAncestorWalk = 48;
constexpr double kEdgeFractionEpsilon = 0.001;
constexpr double kEdgePixelEpsilon = 0.5;

const CFStringRef kManualAccessibilityAttribute = CFSTR("AXManualAccessibility");
const QString kScrollAreaRole = QStringLiteral("AXScrollArea");
const QString kValueIndicatorRole = QStringLiteral("AXValueIndicator");
const QStringList kWalkStopRoles = {
    QStringLiteral("AXWindow"),
    QStringLiteral("AXApplication"),
    QStringLiteral("AXSystemWide"),
};

// Releases a Core Foundation reference when it goes out of scope.
template <typename T>
class ScopedCF
{
public:
    explicit ScopedCF(T ref = nullptr) : m_ref(ref) {}
    ~ScopedCF() { reset(); }
    ScopedCF(const ScopedCF&) = delete;
    ScopedCF& operator=(const ScopedCF&) = delete;

    T get() const { return m_ref; }
    explicit operator bool() const { return m_ref != nullptr; }
    void reset(T ref = nullptr)
    {
        if (m_ref) {
            CFRelease(m_ref);
        }
        m_ref = ref;
    }
    T release()
    {
        T ref = m_ref;
        m_ref = nullptr;
        return ref;
    }

private:
    T m_ref;
};

QJsonObject frameToJson(const CGRect& frame)
{
    return QJsonObject{
        {QStringLiteral("x"), frame.origin.x},
        {QStringLiteral("y"), frame.origin.y},
        {QStringLiteral("width"), frame.size.width},
        {QStringLiteral("height"), frame.size.height},
    };
}

QRect frameToRect(const CGRect& frame)
{
    return QRect(qRound(frame.origin.x), qRound(frame.origin.y),
                 qRound(frame.size.width), qRound(frame.size.height));
}

// First element of an array-valued attribute such as AXContents, retained.
AXUIElementRef copyFirstElementOfArray(AXUIElementRef element, CFStringRef attribute,
                                       const QString& requiredRole = QString())
{
    CFTypeRef valueRef = nullptr;
    if (AXUIElementCopyAttributeValue(element, attribute, &valueRef) != kAXErrorSuccess || !valueRef) {
        return nullptr;
    }
    ScopedCF<CFTypeRef> value(valueRef);
    if (CFGetTypeID(valueRef) != CFArrayGetTypeID()) {
        return nullptr;
    }

    const auto array = static_cast<CFArrayRef>(valueRef);
    for (CFIndex i = 0; i < CFArrayGetCount(array); ++i) {
        const void* item = CFArrayGetValueAtIndex(array, i);
        if (!item || CFGetTypeID(item) != AXUIElementGetTypeID()) {
            continue;
        }
        const auto child = static_cast<AXUIElementRef>(item);
        if (!requiredRole.isEmpty() && getAXElementRole(child) != requiredRole) {
            continue;
        }
        CFRetain(child);
        return child;
    }
    return nullptr;
}

struct WindowHit {
    pid_t processId = 0;
    QRect bounds;
    QString ownerName;
    int layer = 0;
};

// Front-most application window under the point that belongs to another
// process, so the probe's own overlay is never hit-tested.
std::optional<WindowHit> windowUnderPoint(const QPoint& point)
{
    ScopedCF<CFArrayRef> list(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
    if (!list) {
        return std::nullopt;
    }

    const pid_t ownProcessId = getpid();
    // Normal application windows; the Dock, menus and overlays sit higher.
    const int maxApplicationWindowLayer = CGWindowLevelForKey(kCGDockWindowLevelKey) - 1;
    for (CFIndex i = 0; i < CFArrayGetCount(list.get()); ++i) {
        const auto info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list.get(), i));
        int processId = 0;
        int layer = 0;
        double alpha = 1.0;
        const auto pidRef = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowOwnerPID));
        const auto layerRef = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowLayer));
        const auto alphaRef = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowAlpha));
        if (!pidRef || !CFNumberGetValue(pidRef, kCFNumberIntType, &processId) || processId == ownProcessId) {
            continue;
        }
        if (layerRef) {
            CFNumberGetValue(layerRef, kCFNumberIntType, &layer);
        }
        if (alphaRef) {
            CFNumberGetValue(alphaRef, kCFNumberDoubleType, &alpha);
        }
        if (layer < 0 || layer > maxApplicationWindowLayer || alpha <= 0.0) {
            continue;
        }

        CGRect bounds = CGRectZero;
        const auto boundsRef = static_cast<CFDictionaryRef>(CFDictionaryGetValue(info, kCGWindowBounds));
        if (!boundsRef || !CGRectMakeWithDictionaryRepresentation(boundsRef, &bounds) ||
            !CGRectContainsPoint(bounds, CGPointMake(point.x(), point.y()))) {
            continue;
        }

        WindowHit hit;
        hit.processId = static_cast<pid_t>(processId);
        hit.bounds = frameToRect(bounds);
        hit.layer = layer;
        if (const auto nameRef = static_cast<CFStringRef>(CFDictionaryGetValue(info, kCGWindowOwnerName))) {
            hit.ownerName = QString::fromCFString(nameRef);
        }
        return hit;
    }
    return std::nullopt;
}

bool isScrollAreaCandidate(AXUIElementRef element, const QString& role)
{
    if (role == kScrollAreaRole) {
        return true;
    }
    ScopedCF<AXUIElementRef> scrollBar(copyAXElementAttribute(element, kAXVerticalScrollBarAttribute));
    return static_cast<bool>(scrollBar);
}

// Innermost scroll area of an application at a point, retained, or null.
AXUIElementRef copyInnermostScrollAreaAt(pid_t processId, const QPoint& point)
{
    ScopedCF<AXUIElementRef> application(AXUIElementCreateApplication(processId));
    AXUIElementSetMessagingTimeout(application.get(), kAXMessagingTimeoutSeconds);
    AXUIElementRef hitElement = nullptr;
    if (AXUIElementCopyElementAtPosition(application.get(), static_cast<float>(point.x()),
                                         static_cast<float>(point.y()), &hitElement) != kAXErrorSuccess) {
        return nullptr;
    }
    ScopedCF<AXUIElementRef> current(hitElement);
    for (int depth = 0; depth < kMaxAncestorWalk && current; ++depth) {
        AXUIElementSetMessagingTimeout(current.get(), kAXMessagingTimeoutSeconds);
        const QString role = getAXElementRole(current.get());
        if (kWalkStopRoles.contains(role)) {
            break;
        }
        if (isScrollAreaCandidate(current.get(), role)) {
            return current.release();
        }
        current.reset(copyAXElementAttribute(current.get(), kAXParentAttribute));
    }
    return nullptr;
}

class MacScrollArea final : public ScrollArea
{
public:
    MacScrollArea(AXUIElementRef element, pid_t processId, int level, int availableLevels,
                  QString appName, QString appVersion, QJsonObject hitDiagnostics)
        : m_element(element)
        , m_processId(processId)
        , m_level(level)
        , m_availableLevels(availableLevels)
        , m_appName(std::move(appName))
        , m_appVersion(std::move(appVersion))
        , m_hitDiagnostics(std::move(hitDiagnostics))
    {
    }

    AXUIElementRef element() const { return m_element.get(); }
    pid_t processId() const { return m_processId; }

    ScrollAreaSnapshot snapshot() const override
    {
        ScrollAreaSnapshot snapshot;
        snapshot.level = m_level;
        snapshot.availableLevels = m_availableLevels;
        snapshot.processId = m_processId;
        snapshot.appName = m_appName;
        snapshot.appVersion = m_appVersion;
        snapshot.role = getAXElementRole(m_element.get());
        snapshot.diagnostics = m_hitDiagnostics;

        CGRect frame = CGRectZero;
        if (!getAXElementFrame(m_element.get(), frame)) {
            snapshot.diagnostics.insert(QStringLiteral("frameReadable"), false);
            return snapshot;
        }
        snapshot.bounds = frameToRect(frame);
        readPosition(frame, snapshot);
        return snapshot;
    }

private:
    void readPosition(const CGRect& frame, ScrollAreaSnapshot& snapshot) const
    {
        ScrollPosition& position = snapshot.position;
        QJsonObject& diagnostics = snapshot.diagnostics;
        const double viewportHeight = frame.size.height;

        ScopedCF<AXUIElementRef> scrollBar(copyAXElementAttribute(m_element.get(), kAXVerticalScrollBarAttribute));
        diagnostics.insert(QStringLiteral("hasVerticalScrollBar"), static_cast<bool>(scrollBar));
        if (scrollBar) {
            if (const auto value = copyAXNumberAttribute(scrollBar.get(), kAXValueAttribute)) {
                diagnostics.insert(QStringLiteral("scrollBarValue"), *value);
                position.fraction = std::clamp(*value, 0.0, 1.0);
                position.sources << QStringLiteral("AXVerticalScrollBar.AXValue");
            }
            CGRect barFrame = CGRectZero;
            if (getAXElementFrame(scrollBar.get(), barFrame)) {
                diagnostics.insert(QStringLiteral("scrollBarFrame"), frameToJson(barFrame));
                ScopedCF<AXUIElementRef> indicator(
                    copyFirstElementOfArray(scrollBar.get(), kAXChildrenAttribute, kValueIndicatorRole));
                CGRect indicatorFrame = CGRectZero;
                if (indicator && getAXElementFrame(indicator.get(), indicatorFrame) && barFrame.size.height > 0) {
                    diagnostics.insert(QStringLiteral("valueIndicatorFrame"), frameToJson(indicatorFrame));
                    position.viewportRatio = std::clamp(indicatorFrame.size.height / barFrame.size.height, 0.0, 1.0);
                    position.sources << QStringLiteral("AXValueIndicator");
                }
            }
        }

        // NSScrollView exposes its document view as AXContents; its frame
        // moves up as the view scrolls down.
        ScopedCF<AXUIElementRef> contents(copyFirstElementOfArray(m_element.get(), kAXContentsAttribute));
        CGRect contentsFrame = CGRectZero;
        if (contents && getAXElementFrame(contents.get(), contentsFrame)) {
            diagnostics.insert(QStringLiteral("contentsRole"), getAXElementRole(contents.get()));
            diagnostics.insert(QStringLiteral("contentsFrame"), frameToJson(contentsFrame));
            if (contentsFrame.size.height + kEdgePixelEpsilon >= viewportHeight) {
                position.contentHeight = contentsFrame.size.height;
                position.offset = frame.origin.y - contentsFrame.origin.y;
                position.sources << QStringLiteral("AXContents");
            }
        }

        if (!position.contentHeight && position.viewportRatio && *position.viewportRatio > 0.0) {
            position.contentHeight = viewportHeight / *position.viewportRatio;
        }
        if (position.contentHeight && !position.viewportRatio && *position.contentHeight > 0.0) {
            position.viewportRatio = std::min(1.0, viewportHeight / *position.contentHeight);
        }
        const std::optional<double> scrollRange = position.contentHeight
            ? std::optional<double>(std::max(0.0, *position.contentHeight - viewportHeight))
            : std::nullopt;
        if (!position.fraction && position.offset && scrollRange && *scrollRange > 0.0) {
            position.fraction = std::clamp(*position.offset / *scrollRange, 0.0, 1.0);
        }
        if (!position.offset && position.fraction && scrollRange) {
            position.offset = *position.fraction * *scrollRange;
        }

        if (scrollRange && *scrollRange <= kEdgePixelEpsilon) {
            position.atTop = true;
            position.atBottom = true;
        } else if (position.fraction) {
            position.atTop = *position.fraction <= kEdgeFractionEpsilon;
            position.atBottom = *position.fraction >= 1.0 - kEdgeFractionEpsilon;
        } else if (position.offset && scrollRange) {
            position.atTop = *position.offset <= kEdgePixelEpsilon;
            position.atBottom = *position.offset >= *scrollRange - kEdgePixelEpsilon;
        }
    }

    ScopedCF<AXUIElementRef> m_element;
    pid_t m_processId = 0;
    int m_level = 0;
    int m_availableLevels = 0;
    QString m_appName;
    QString m_appVersion;
    QJsonObject m_hitDiagnostics;
};

class MacScrollAreaLocator final : public ScrollAreaLocator
{
public:
    bool isAvailable() const override { return true; }

    LocateResult locateAt(const QPoint& globalPos, const LocateOptions& options) override
    {
        if (!AXIsProcessTrusted()) {
            LocateResult result;
            result.status = LocateStatus::PermissionDenied;
            result.detail = QStringLiteral(
                "Accessibility permission is required (System Settings > Privacy & Security > Accessibility)");
            return result;
        }

        const int maxAttempts = std::max(1, options.maxAttempts);
        LocateResult result;
        for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
            result = locateOnce(globalPos, options.level);
            result.attempts = attempt;
            if (result.status != LocateStatus::NoScrollArea || attempt == maxAttempts) {
                break;
            }
            QThread::msleep(static_cast<unsigned long>(std::max(0, options.retryDelayMs)));
        }
        return result;
    }

    bool enableEnhancedAccessibility(qint64 processId, QString* detail) override
    {
        ScopedCF<AXUIElementRef> application(AXUIElementCreateApplication(static_cast<pid_t>(processId)));
        AXUIElementSetMessagingTimeout(application.get(), kAXMessagingTimeoutSeconds);
        const AXError error = AXUIElementSetAttributeValue(
            application.get(), kManualAccessibilityAttribute, kCFBooleanTrue);
        if (detail) {
            *detail = QStringLiteral("AXManualAccessibility AXError %1").arg(static_cast<int>(error));
        }
        return error == kAXErrorSuccess;
    }

    QJsonObject environment() const override
    {
        QJsonObject environment;
        @autoreleasepool {
            NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
            NSString* showScrollBars = [defaults stringForKey:@"AppleShowScrollBars"];
            environment.insert(QStringLiteral("appleShowScrollBars"),
                               showScrollBars ? QString::fromNSString(showScrollBars)
                                              : QStringLiteral("Automatic (default)"));
            environment.insert(QStringLiteral("preferredScrollerStyle"),
                               [NSScroller preferredScrollerStyle] == NSScrollerStyleOverlay
                                   ? QStringLiteral("overlay")
                                   : QStringLiteral("legacy"));
            if ([defaults objectForKey:@"com.apple.swipescrolldirection"]) {
                environment.insert(QStringLiteral("naturalScrolling"),
                                   static_cast<bool>([defaults boolForKey:@"com.apple.swipescrolldirection"]));
            }
        }
        environment.insert(QStringLiteral("accessibilityTrusted"), static_cast<bool>(AXIsProcessTrusted()));
        environment.insert(QStringLiteral("postEventAccess"), static_cast<bool>(CGPreflightPostEventAccess()));
        environment.insert(QStringLiteral("screenCaptureAccess"), static_cast<bool>(CGPreflightScreenCaptureAccess()));
        return environment;
    }

private:
    static void readApplicationInfo(pid_t processId, QString* name, QString* version)
    {
        @autoreleasepool {
            NSRunningApplication* application =
                [NSRunningApplication runningApplicationWithProcessIdentifier:processId];
            if (application.localizedName) {
                *name = QString::fromNSString(application.localizedName);
            }
            if (application.bundleURL) {
                NSBundle* bundle = [NSBundle bundleWithURL:application.bundleURL];
                id shortVersion = [bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
                if ([shortVersion isKindOfClass:[NSString class]]) {
                    *version = QString::fromNSString(shortVersion);
                }
            }
        }
    }

    LocateResult locateOnce(const QPoint& globalPos, int level) const
    {
        LocateResult result;
        const std::optional<WindowHit> window = windowUnderPoint(globalPos);
        if (!window) {
            result.status = LocateStatus::NoWindow;
            result.detail = QStringLiteral("no application window under the point");
            return result;
        }
        result.processId = window->processId;

        QJsonObject hitDiagnostics{
            {QStringLiteral("windowOwner"), window->ownerName},
            {QStringLiteral("windowLayer"), window->layer},
            {QStringLiteral("windowBounds"), QJsonObject{
                {QStringLiteral("x"), window->bounds.x()},
                {QStringLiteral("y"), window->bounds.y()},
                {QStringLiteral("width"), window->bounds.width()},
                {QStringLiteral("height"), window->bounds.height()},
            }},
        };

        ScopedCF<AXUIElementRef> application(AXUIElementCreateApplication(window->processId));
        AXUIElementSetMessagingTimeout(application.get(), kAXMessagingTimeoutSeconds);
        AXUIElementRef hitElement = nullptr;
        const AXError hitError = AXUIElementCopyElementAtPosition(
            application.get(), static_cast<float>(globalPos.x()), static_cast<float>(globalPos.y()), &hitElement);
        ScopedCF<AXUIElementRef> current(hitElement);
        hitDiagnostics.insert(QStringLiteral("hitTestError"), static_cast<int>(hitError));
        if (hitError == kAXErrorAPIDisabled) {
            result.status = LocateStatus::PermissionDenied;
            result.detail = QStringLiteral("the accessibility API is disabled for this process");
            return result;
        }
        if (hitError != kAXErrorSuccess || !current) {
            result.status = LocateStatus::NoScrollArea;
            result.detail = QStringLiteral("accessibility hit test failed (AXError %1)").arg(static_cast<int>(hitError));
            return result;
        }

        // Walk from the hit element up to its window, collecting every
        // scroll area (innermost first).
        std::vector<AXUIElementRef> candidates;
        QJsonArray rolePath;
        for (int depth = 0; depth < kMaxAncestorWalk && current; ++depth) {
            AXUIElementSetMessagingTimeout(current.get(), kAXMessagingTimeoutSeconds);
            const QString role = getAXElementRole(current.get());
            rolePath.append(role);
            if (kWalkStopRoles.contains(role)) {
                break;
            }
            if (isScrollAreaCandidate(current.get(), role)) {
                CFRetain(current.get());
                candidates.push_back(current.get());
            }
            current.reset(copyAXElementAttribute(current.get(), kAXParentAttribute));
        }
        hitDiagnostics.insert(QStringLiteral("rolePath"), rolePath);

        const int available = static_cast<int>(candidates.size());
        AXUIElementRef chosen = nullptr;
        for (int index = 0; index < available; ++index) {
            if (index == level) {
                chosen = candidates[static_cast<size_t>(index)];
            } else {
                CFRelease(candidates[static_cast<size_t>(index)]);
            }
        }
        if (!chosen) {
            result.status = LocateStatus::NoScrollArea;
            result.detail = available == 0
                ? QStringLiteral("no AXScrollArea or vertical scroll bar between the point and its window")
                : QStringLiteral("only %1 nested scroll area(s) under the point").arg(available);
            return result;
        }

        QString appName = window->ownerName;
        QString appVersion;
        readApplicationInfo(window->processId, &appName, &appVersion);
        result.status = LocateStatus::Found;
        result.area = std::make_unique<MacScrollArea>(
            chosen, window->processId, level, available, appName, appVersion, hitDiagnostics);
        return result;
    }
};

class MacScrollInjector final : public ScrollInjector
{
public:
    QList<ScrollMethod> supportedMethods() const override
    {
        return {ScrollMethod::WheelPixels, ScrollMethod::WheelPixelsContinuous};
    }

    InjectResult scroll(const ScrollArea& area, const ScrollRequest& request) override
    {
        if (!supportedMethods().contains(request.method)) {
            return InjectResult::failed(QStringLiteral("%1 is not available on macOS").arg(QLatin1String(scrollMethodName(request.method))));
        }
        if (!CGPreflightPostEventAccess()) {
            return InjectResult::failed(QStringLiteral("posting events requires Accessibility permission"));
        }

        const auto* macArea = dynamic_cast<const MacScrollArea*>(&area);
        if (!macArea) {
            return InjectResult::failed(QStringLiteral("not a macOS scroll area"));
        }
        const ScrollAreaSnapshot snapshot = area.snapshot();
        if (snapshot.bounds.isEmpty()) {
            return InjectResult::failed(QStringLiteral("scroll area bounds are not readable"));
        }
        // Wheel input goes to the innermost scroll view under the point, so
        // use a point where that is the selected area itself.
        std::optional<QPoint> chosen;
        for (const QPoint& point : scrollTargetPoints(snapshot.bounds)) {
            const std::optional<WindowHit> window = windowUnderPoint(point);
            if (!window || window->processId != macArea->processId()) {
                continue;
            }
            ScopedCF<AXUIElementRef> innermost(copyInnermostScrollAreaAt(macArea->processId(), point));
            if (innermost && CFEqual(innermost.get(), macArea->element())) {
                chosen = point;
                break;
            }
        }
        if (!chosen) {
            return InjectResult::failed(QStringLiteral("no point inside the selected scroll area reaches it: "
                                                       "nested scroll areas or other windows cover every probe point"));
        }
        const QPoint centre = *chosen;
        const CGPoint target = CGPointMake(centre.x(), centre.y());

        // Positive wheel deltas scroll toward the top, so negate.
        const int32_t delta = static_cast<int32_t>(std::clamp<qint64>(
            -static_cast<qint64>(request.amount),
            std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
        ScopedCF<CGEventRef> event(CGEventCreateScrollWheelEvent2(nullptr, kCGScrollEventUnitPixel, 1, delta, 0, 0));
        if (!event) {
            return InjectResult::failed(QStringLiteral("CGEventCreateScrollWheelEvent2 failed"));
        }
        CGEventSetLocation(event.get(), target);
        if (request.method == ScrollMethod::WheelPixelsContinuous) {
            CGEventSetIntegerValueField(event.get(), kCGScrollWheelEventIsContinuous, 1);
        }

        if (request.postToProcess) {
            const auto dispatchedAt = std::chrono::steady_clock::now();
            CGEventPostToPid(static_cast<pid_t>(snapshot.processId), event.get());
            return {true, QStringLiteral("posted to pid %1").arg(snapshot.processId), dispatchedAt};
        }
        CGWarpMouseCursorPosition(target);
        CGAssociateMouseAndMouseCursorPosition(true);
        const auto dispatchedAt = std::chrono::steady_clock::now();
        CGEventPost(kCGHIDEventTap, event.get());
        return {true, QStringLiteral("posted at (%1, %2)").arg(centre.x()).arg(centre.y()), dispatchedAt};
    }
};

} // namespace

std::unique_ptr<ScrollAreaLocator> ScrollAreaLocator::create()
{
    return std::make_unique<MacScrollAreaLocator>();
}

std::unique_ptr<ScrollInjector> ScrollInjector::create()
{
    return std::make_unique<MacScrollInjector>();
}

} // namespace snaptray::scrollcapture
