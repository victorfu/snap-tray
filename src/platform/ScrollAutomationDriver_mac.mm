#include "platform/ScrollAutomationDriverFactory.h"
#include "platform/IScrollAutomationDriver.h"

#import <ApplicationServices/ApplicationServices.h>
#import <AppKit/AppKit.h>

#include <QDebug>
#include <QtGlobal>
#include <optional>

namespace {

enum class DriverMode {
    None,
    Value,
    ScrollAction,
    Increment,
    SyntheticWheel,
};

constexpr float kAXMessagingTimeoutSeconds = 0.12f;
constexpr CFTimeInterval kAXProbeBudgetSeconds = 0.25;
constexpr int kAXFailureFallbackThreshold = 2;
constexpr int kSyntheticPixelStepY = -260;
constexpr int kSyntheticLineStepY = -10;
constexpr int kSyntheticEstimatedDeltaY = 180;
constexpr double kValueModeStepRatio = 0.09;

std::optional<double> copyNumericAttribute(AXUIElementRef element, CFStringRef attribute)
{
    if (!element || !attribute) {
        return std::nullopt;
    }

    CFTypeRef valueRef = nullptr;
    AXError err = AXUIElementCopyAttributeValue(element, attribute, &valueRef);
    if (err != kAXErrorSuccess || !valueRef) {
        return std::nullopt;
    }

    std::optional<double> result;
    if (CFGetTypeID(valueRef) == CFNumberGetTypeID()) {
        double value = 0.0;
        if (CFNumberGetValue(static_cast<CFNumberRef>(valueRef), kCFNumberDoubleType, &value)) {
            result = value;
        }
    }

    CFRelease(valueRef);
    return result;
}

bool setNumericAttribute(AXUIElementRef element, CFStringRef attribute, double value)
{
    if (!element || !attribute) {
        return false;
    }

    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &value);
    if (!number) {
        return false;
    }
    const AXError err = AXUIElementSetAttributeValue(element, attribute, number);
    CFRelease(number);
    return err == kAXErrorSuccess;
}

bool elementSupportsAction(AXUIElementRef element, CFStringRef action)
{
    if (!element || !action) {
        return false;
    }

    CFArrayRef actions = nullptr;
    AXError err = AXUIElementCopyActionNames(element, &actions);
    if (err != kAXErrorSuccess || !actions) {
        return false;
    }

    const bool supported = CFArrayContainsValue(actions,
                                                CFRangeMake(0, CFArrayGetCount(actions)),
                                                action);
    CFRelease(actions);
    return supported;
}

bool isValueAttributeSettable(AXUIElementRef element)
{
    if (!element) {
        return false;
    }

    Boolean settable = false;
    const AXError err = AXUIElementIsAttributeSettable(element, kAXValueAttribute, &settable);
    return err == kAXErrorSuccess && settable;
}

CFStringRef copySupportedScrollAction(AXUIElementRef element)
{
    if (!element) {
        return nullptr;
    }

    static const CFStringRef kScrollActions[] = {
        CFSTR("AXScrollDownByPage"),
        CFSTR("AXScrollDownByLine"),
        CFSTR("AXScrollDownByIncrement")
    };

    for (CFStringRef action : kScrollActions) {
        if (elementSupportsAction(element, action)) {
            return static_cast<CFStringRef>(CFRetain(action));
        }
    }

    return nullptr;
}

AXUIElementRef copyParent(AXUIElementRef element)
{
    if (!element) {
        return nullptr;
    }

    CFTypeRef parentRef = nullptr;
    AXError err = AXUIElementCopyAttributeValue(element, kAXParentAttribute, &parentRef);
    if (err != kAXErrorSuccess || !parentRef) {
        return nullptr;
    }

    if (CFGetTypeID(parentRef) != AXUIElementGetTypeID()) {
        CFRelease(parentRef);
        return nullptr;
    }

    return static_cast<AXUIElementRef>(parentRef);
}

void setAXMessagingTimeout(AXUIElementRef element, float timeoutSeconds)
{
    if (!element) {
        return;
    }
    AXUIElementSetMessagingTimeout(element, timeoutSeconds);
}

bool postMouseClickAt(const QPoint &globalPoint)
{
    const CGPoint cgPoint = CGPointMake(globalPoint.x(), globalPoint.y());
    CGEventRef down = CGEventCreateMouseEvent(nullptr, kCGEventLeftMouseDown, cgPoint, kCGMouseButtonLeft);
    CGEventRef up = CGEventCreateMouseEvent(nullptr, kCGEventLeftMouseUp, cgPoint, kCGMouseButtonLeft);
    if (!down || !up) {
        if (down) {
            CFRelease(down);
        }
        if (up) {
            CFRelease(up);
        }
        return false;
    }

    CGEventPost(kCGHIDEventTap, down);
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(down);
    CFRelease(up);
    return true;
}

bool postScrollWheelAt(const QPoint &globalPoint, CGScrollEventUnit unit, int delta)
{
    CGEventRef scroll = CGEventCreateScrollWheelEvent(nullptr, unit, 1, delta);
    if (!scroll) {
        return false;
    }

    CGEventSetLocation(scroll, CGPointMake(globalPoint.x(), globalPoint.y()));
    CGEventPost(kCGHIDEventTap, scroll);
    CFRelease(scroll);
    return true;
}

bool postScrollWheelToPid(const QPoint &globalPoint, CGScrollEventUnit unit, int delta, pid_t targetPid)
{
    if (targetPid <= 0) {
        return false;
    }

    CGEventRef scroll = CGEventCreateScrollWheelEvent(nullptr, unit, 1, delta);
    if (!scroll) {
        return false;
    }

    CGEventSetLocation(scroll, CGPointMake(globalPoint.x(), globalPoint.y()));
    CGEventPostToPid(targetPid, scroll);
    CFRelease(scroll);
    return true;
}

bool postScrollWheelPixelAt(const QPoint &globalPoint, int pixelDelta)
{
    return postScrollWheelAt(globalPoint, kCGScrollEventUnitPixel, pixelDelta);
}

bool postScrollWheelLineAt(const QPoint &globalPoint, int lineDelta)
{
    return postScrollWheelAt(globalPoint, kCGScrollEventUnitLine, lineDelta);
}

bool preferSyntheticForProcess(pid_t pid)
{
    if (pid <= 0) {
        return false;
    }

    NSRunningApplication *application = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    if (!application) {
        return false;
    }

    NSString *bundleId = [[application bundleIdentifier] lowercaseString];
    NSString *localizedName = [[application localizedName] lowercaseString];
    if (!bundleId) {
        bundleId = @"";
    }
    if (!localizedName) {
        localizedName = @"";
    }

    const bool electronLike =
           [bundleId containsString:@"slack"] ||
           [localizedName containsString:@"slack"] ||
           [bundleId containsString:@"electron"] ||
           [localizedName containsString:@"electron"];
    if (electronLike) {
        return true;
    }

    const bool browserLike =
           [bundleId containsString:@"safari"] ||
           [bundleId containsString:@"chrome"] ||
           [bundleId containsString:@"firefox"] ||
           [bundleId containsString:@"edge"] ||
           [bundleId containsString:@"arc"] ||
           [bundleId containsString:@"brave"] ||
           [bundleId containsString:@"opera"] ||
           [bundleId containsString:@"vivaldi"] ||
           [localizedName containsString:@"safari"] ||
           [localizedName containsString:@"chrome"] ||
           [localizedName containsString:@"firefox"] ||
           [localizedName containsString:@"edge"] ||
           [localizedName containsString:@"arc"] ||
           [localizedName containsString:@"brave"] ||
           [localizedName containsString:@"opera"] ||
           [localizedName containsString:@"vivaldi"];
    return browserLike;
}

class ScrollAutomationDriverMac final : public IScrollAutomationDriver
{
public:
    explicit ScrollAutomationDriverMac(QObject *parent = nullptr)
        : IScrollAutomationDriver(parent)
    {
    }

    ~ScrollAutomationDriverMac() override
    {
        reset();
    }

    ScrollProbeResult probeAt(const QPoint &globalPoint, QScreen *screen) override
    {
        Q_UNUSED(screen);
        reset();

        ScrollProbeResult result;
        result.mode = ScrollAutomationMode::ManualOnly;
        result.anchorPoint = globalPoint;
        m_anchorPoint = globalPoint;
        m_hasAnchorPoint = true;
        m_targetPid = 0;
        m_preferGlobalSyntheticInjection = false;

        NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @NO};
        if (!AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options)) {
            result.reason = QStringLiteral("Accessibility permission is unavailable");
            return result;
        }

        AXUIElementRef systemWide = AXUIElementCreateSystemWide();
        if (!systemWide) {
            result.reason = QStringLiteral("Failed to create accessibility context");
            return result;
        }
        setAXMessagingTimeout(systemWide, kAXMessagingTimeoutSeconds);

        AXUIElementRef hitElement = nullptr;
        const AXError hitErr = AXUIElementCopyElementAtPosition(
            systemWide,
            static_cast<float>(globalPoint.x()),
            static_cast<float>(globalPoint.y()),
            &hitElement);
        CFRelease(systemWide);

        if (hitErr != kAXErrorSuccess || !hitElement) {
            m_mode = DriverMode::SyntheticWheel;
            result.mode = ScrollAutomationMode::AutoSynthetic;
            result.reason = (hitErr == kAXErrorCannotComplete)
                ? QStringLiteral("Auto probe timeout; using synthetic wheel fallback")
                : QStringLiteral("Using synthetic wheel fallback");
            result.focusRecommended = true;
            return result;
        }
        setAXMessagingTimeout(hitElement, kAXMessagingTimeoutSeconds);

        pid_t hitPid = 0;
        AXUIElementGetPid(hitElement, &hitPid);
        const bool preferSyntheticForApp = preferSyntheticForProcess(hitPid);
        m_targetPid = hitPid;
        if (preferSyntheticForApp) {
            m_mode = DriverMode::SyntheticWheel;
            result.mode = ScrollAutomationMode::AutoSynthetic;
            result.reason = QStringLiteral("Browser/Electron target detected; using synthetic wheel fallback");
            result.focusRecommended = true;
            m_preferGlobalSyntheticInjection = true;
            CFRelease(hitElement);
            return result;
        }

        AXUIElementRef current = hitElement;
        const CFAbsoluteTime probeStart = CFAbsoluteTimeGetCurrent();
        bool timedOut = false;
        constexpr int kMaxDepth = 64;
        for (int depth = 0; depth < kMaxDepth && current; ++depth) {
            if (CFAbsoluteTimeGetCurrent() - probeStart > kAXProbeBudgetSeconds) {
                timedOut = true;
                break;
            }

            setAXMessagingTimeout(current, kAXMessagingTimeoutSeconds);
            const auto value = copyNumericAttribute(current, kAXValueAttribute);
            const auto maxValue = copyNumericAttribute(current, kAXMaxValueAttribute);
            const auto minValue = copyNumericAttribute(current, kAXMinValueAttribute);
            if (value.has_value() && maxValue.has_value() && minValue.has_value() &&
                (*maxValue - *minValue) > 0.0 &&
                isValueAttributeSettable(current)) {
                m_scrollElement = static_cast<AXUIElementRef>(CFRetain(current));
                setAXMessagingTimeout(m_scrollElement, kAXMessagingTimeoutSeconds);
                m_mode = DriverMode::Value;
                m_lastValue = *value;
                m_hasLastValue = true;
                break;
            }

            if (CFStringRef action = copySupportedScrollAction(current)) {
                m_scrollElement = static_cast<AXUIElementRef>(CFRetain(current));
                setAXMessagingTimeout(m_scrollElement, kAXMessagingTimeoutSeconds);
                m_scrollAction = action;
                m_mode = DriverMode::ScrollAction;
                break;
            }

            if (elementSupportsAction(current, kAXIncrementAction)) {
                m_scrollElement = static_cast<AXUIElementRef>(CFRetain(current));
                setAXMessagingTimeout(m_scrollElement, kAXMessagingTimeoutSeconds);
                m_mode = DriverMode::Increment;
                break;
            }

            AXUIElementRef parent = copyParent(current);
            if (parent) {
                setAXMessagingTimeout(parent, kAXMessagingTimeoutSeconds);
            }
            CFRelease(current);
            current = parent;
        }

        if (current) {
            CFRelease(current);
        }

        if (!m_scrollElement || m_mode == DriverMode::None) {
            m_mode = DriverMode::SyntheticWheel;
            result.mode = ScrollAutomationMode::AutoSynthetic;
            if (timedOut) {
                result.reason = QStringLiteral("Auto probe timeout; using synthetic wheel fallback");
            } else if (preferSyntheticForApp) {
                result.reason = QStringLiteral("Electron/Slack target detected; using synthetic wheel fallback");
            } else {
                result.reason = QStringLiteral("Using synthetic wheel fallback");
            }
            result.focusRecommended = true;
            return result;
        }

        result.mode = ScrollAutomationMode::Auto;
        switch (m_mode) {
        case DriverMode::Value:
            result.reason = QStringLiteral("Accessibility value scroll driver active");
            break;
        case DriverMode::ScrollAction:
            result.reason = QStringLiteral("Accessibility action scroll driver active");
            break;
        case DriverMode::Increment:
            result.reason = QStringLiteral("Accessibility increment scroll driver active");
            break;
        default:
            result.reason = QStringLiteral("Accessibility scroll driver active");
            break;
        }
        return result;
    }

    ScrollStepResult step() override
    {
        ScrollStepResult result;
        result.driverMode = (m_mode == DriverMode::SyntheticWheel)
            ? ScrollDriverMode::SyntheticGlobal
            : ScrollDriverMode::NativeAccessibility;
        auto maybeFallbackFromAXFailure = [this, &result](AXError err, const QString &context) {
            const bool timeoutLike = (err == kAXErrorCannotComplete ||
                                      err == kAXErrorFailure ||
                                      err == kAXErrorAPIDisabled);
            if (!timeoutLike) {
                result.status = ScrollStepStatus::Failed;
                result.error = context;
                return false;
            }

            ++m_consecutiveAxFailures;
            if (m_consecutiveAxFailures >= kAXFailureFallbackThreshold && forceSyntheticFallback()) {
                qWarning() << "ScrollAutomationDriverMac: AX step degraded, switching to synthetic fallback:"
                           << context << "err:" << static_cast<int>(err);
                result.status = ScrollStepStatus::Stepped;
                result.estimatedDeltaY = 0;
                result.estimatedDeltaYConfidence = 0.0;
                result.error = context + QStringLiteral(" -> switched to synthetic fallback");
                result.driverMode = ScrollDriverMode::SyntheticGlobal;
                return true;
            }

            result.status = ScrollStepStatus::EndReached;
            result.error = context;
            return false;
        };

        if (m_mode == DriverMode::SyntheticWheel) {
            if (!m_hasAnchorPoint) {
                result.error = QStringLiteral("Scroll probe anchor unavailable");
                return result;
            }

            const auto injectTargeted = [this](bool *locked) {
                bool injectedTargeted = false;
                if (m_targetPid > 0) {
                    injectedTargeted = postScrollWheelToPid(m_anchorPoint, kCGScrollEventUnitPixel, kSyntheticPixelStepY, m_targetPid);
                    if (!injectedTargeted) {
                        injectedTargeted = postScrollWheelToPid(m_anchorPoint, kCGScrollEventUnitLine, kSyntheticLineStepY, m_targetPid);
                    }
                }
                if (locked) {
                    *locked = injectedTargeted;
                }
                return injectedTargeted;
            };

            const auto injectGlobal = [this]() {
                bool injectedGlobal = postScrollWheelPixelAt(m_anchorPoint, kSyntheticPixelStepY);
                if (!injectedGlobal) {
                    injectedGlobal = postScrollWheelLineAt(m_anchorPoint, kSyntheticLineStepY);
                }
                return injectedGlobal;
            };

            // For browser/electron targets we intentionally avoid relying on pid-targeted
            // dispatch because delivery cannot be observed and may report false success.
            if (m_preferGlobalSyntheticInjection && !m_focusInjected) {
                focusTarget();
            }

            const bool preferGlobalFirst = m_preferGlobalSyntheticInjection || m_focusInjected;
            bool targetLocked = false;
            bool injected = false;

            if (preferGlobalFirst) {
                injected = injectGlobal();
                if (!injected && !m_preferGlobalSyntheticInjection) {
                    injected = injectTargeted(&targetLocked);
                }
            } else {
                injected = injectTargeted(&targetLocked);
                if (!injected) {
                    injected = injectGlobal();
                }
            }

            // Do not force-refocus on every step: that can steal app focus and hide
            // control UI unexpectedly. Only retry once with focus when injection fails.
            if (!injected) {
                const bool focused = focusTarget();
                if (focused) {
                    injected = injectGlobal();
                    if (!injected && !m_preferGlobalSyntheticInjection) {
                        injected = injectTargeted(&targetLocked);
                    }
                }
            }

            if (!injected) {
                result.error = QStringLiteral("Failed to inject synthetic wheel event");
                return result;
            }

            result.status = ScrollStepStatus::Stepped;
            result.estimatedDeltaY = kSyntheticEstimatedDeltaY;
            result.estimatedDeltaYConfidence = 0.35;
            result.inputInjected = true;
            result.targetLocked = targetLocked;
            result.driverMode = targetLocked
                ? ScrollDriverMode::SyntheticTargeted
                : ScrollDriverMode::SyntheticGlobal;
            m_consecutiveAxFailures = 0;
            return result;
        }

        if (!m_scrollElement || m_mode == DriverMode::None) {
            result.error = QStringLiteral("Scroll probe not initialized");
            return result;
        }

        if (m_mode == DriverMode::Value) {
            const auto currentValue = copyNumericAttribute(m_scrollElement, kAXValueAttribute);
            const auto minValue = copyNumericAttribute(m_scrollElement, kAXMinValueAttribute);
            const auto maxValue = copyNumericAttribute(m_scrollElement, kAXMaxValueAttribute);
            if (!currentValue.has_value() || !minValue.has_value() || !maxValue.has_value()) {
                if (maybeFallbackFromAXFailure(kAXErrorCannotComplete, QStringLiteral("Failed to query scroll value"))) {
                    return result;
                }
                return result;
            }

            if (*maxValue - *minValue <= 0.0 || *currentValue >= (*maxValue - 1e-4)) {
                result.status = ScrollStepStatus::EndReached;
                m_consecutiveAxFailures = 0;
                return result;
            }

            const double delta = qMax((*maxValue - *minValue) * kValueModeStepRatio, 1.0);
            const double nextValue = qMin(*maxValue, *currentValue + delta);
            if (!setNumericAttribute(m_scrollElement, kAXValueAttribute, nextValue)) {
                if (maybeFallbackFromAXFailure(kAXErrorCannotComplete, QStringLiteral("Failed to set scroll value"))) {
                    return result;
                }
                return result;
            }

            const auto newValue = copyNumericAttribute(m_scrollElement, kAXValueAttribute);
            if (!newValue.has_value() || *newValue <= *currentValue + 1e-4) {
                if (maybeFallbackFromAXFailure(kAXErrorCannotComplete, QStringLiteral("Failed to observe updated scroll value"))) {
                    return result;
                }
                return result;
            }

            m_lastValue = *newValue;
            m_hasLastValue = true;
            result.status = ScrollStepStatus::Stepped;
            result.estimatedDeltaY = static_cast<int>((*newValue - *currentValue) * 100.0);
            result.estimatedDeltaYConfidence = 0.80;
            result.targetLocked = true;
            result.driverMode = ScrollDriverMode::NativeAccessibility;
            m_consecutiveAxFailures = 0;
            return result;
        }

        if (m_mode == DriverMode::ScrollAction) {
            if (!m_scrollAction) {
                result.status = ScrollStepStatus::Failed;
                result.error = QStringLiteral("Accessibility scroll action unavailable");
                return result;
            }

            const AXError err = AXUIElementPerformAction(m_scrollElement, m_scrollAction);
            if (err == kAXErrorSuccess) {
                result.status = ScrollStepStatus::Stepped;
                result.estimatedDeltaY = 0;
                result.estimatedDeltaYConfidence = 0.20;
                result.targetLocked = true;
                result.driverMode = ScrollDriverMode::NativeAccessibility;
                m_consecutiveAxFailures = 0;
            } else {
                if (maybeFallbackFromAXFailure(err, QStringLiteral("AX scroll action failed"))) {
                    return result;
                }
            }
            return result;
        }

        const AXError err = AXUIElementPerformAction(m_scrollElement, kAXIncrementAction);
        if (err == kAXErrorSuccess) {
            result.status = ScrollStepStatus::Stepped;
            result.estimatedDeltaY = 0;
            result.estimatedDeltaYConfidence = 0.20;
            result.targetLocked = true;
            result.driverMode = ScrollDriverMode::NativeAccessibility;
            m_consecutiveAxFailures = 0;
        } else {
            if (maybeFallbackFromAXFailure(err, QStringLiteral("AX increment action failed"))) {
                return result;
            }
        }
        return result;
    }

    bool focusTarget() override
    {
        if (!m_hasAnchorPoint) {
            return false;
        }

        const bool ok = postMouseClickAt(m_anchorPoint);
        if (ok) {
            m_focusInjected = true;
            m_syntheticStepsSinceFocus = 0;
        }
        return ok;
    }

    bool forceSyntheticFallback() override
    {
        if (!m_hasAnchorPoint) {
            return false;
        }

        if (m_scrollElement) {
            CFRelease(m_scrollElement);
            m_scrollElement = nullptr;
        }
        if (m_scrollAction) {
            CFRelease(m_scrollAction);
            m_scrollAction = nullptr;
        }

        m_mode = DriverMode::SyntheticWheel;
        m_focusInjected = false;
        m_syntheticStepsSinceFocus = 0;
        m_consecutiveAxFailures = 0;
        return true;
    }

    void reset() override
    {
        if (m_scrollElement) {
            CFRelease(m_scrollElement);
            m_scrollElement = nullptr;
        }
        if (m_scrollAction) {
            CFRelease(m_scrollAction);
            m_scrollAction = nullptr;
        }
        m_mode = DriverMode::None;
        m_lastValue = 0.0;
        m_hasLastValue = false;
        m_anchorPoint = QPoint();
        m_hasAnchorPoint = false;
        m_targetPid = 0;
        m_focusInjected = false;
        m_syntheticStepsSinceFocus = 0;
        m_consecutiveAxFailures = 0;
        m_preferGlobalSyntheticInjection = false;
    }

private:
    AXUIElementRef m_scrollElement = nullptr;
    CFStringRef m_scrollAction = nullptr;
    DriverMode m_mode = DriverMode::None;
    double m_lastValue = 0.0;
    bool m_hasLastValue = false;
    QPoint m_anchorPoint;
    bool m_hasAnchorPoint = false;
    pid_t m_targetPid = 0;
    bool m_focusInjected = false;
    int m_syntheticStepsSinceFocus = 0;
    int m_consecutiveAxFailures = 0;
    bool m_preferGlobalSyntheticInjection = false;
};

} // namespace

IScrollAutomationDriver* createPlatformScrollAutomationDriver(QObject *parent)
{
    return new ScrollAutomationDriverMac(parent);
}

bool isPlatformScrollAutomationAvailable()
{
    return true;
}
