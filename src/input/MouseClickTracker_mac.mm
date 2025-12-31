#include "input/MouseClickTracker.h"

#import <ApplicationServices/ApplicationServices.h>
#import <Cocoa/Cocoa.h>

#include <QDebug>
#include <QThread>

/**
 * @brief macOS implementation using CGEvent tap.
 *
 * This implementation uses a CGEvent tap to listen for mouse events
 * at the system level. It requires Accessibility permission to work.
 */
class MacMouseClickTracker : public MouseClickTracker
{
public:
    explicit MacMouseClickTracker(QObject *parent = nullptr)
        : MouseClickTracker(parent)
        , m_eventTap(nullptr)
        , m_runLoopSource(nullptr)
        , m_running(false)
    {
    }

    ~MacMouseClickTracker() override
    {
        stop();
    }

    bool start() override
    {
        if (m_running) {
            return true;
        }

        // Check for Accessibility permission
        NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @NO};
        bool trusted = AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);

        if (!trusted) {
            qWarning() << "MouseClickTracker: Accessibility permission not granted";
            // Don't prompt here - let the app handle permission requests
            return false;
        }

        // Create event mask for mouse clicks
        CGEventMask eventMask = CGEventMaskBit(kCGEventLeftMouseDown) |
                                CGEventMaskBit(kCGEventLeftMouseUp) |
                                CGEventMaskBit(kCGEventRightMouseDown) |
                                CGEventMaskBit(kCGEventRightMouseUp);

        // Create event tap (listen-only, doesn't block events)
        m_eventTap = CGEventTapCreate(
            kCGHIDEventTap,           // Tap at HID level
            kCGHeadInsertEventTap,    // Insert at head
            kCGEventTapOptionListenOnly,  // Listen only, don't filter
            eventMask,
            eventCallback,
            this
        );

        if (!m_eventTap) {
            qWarning() << "MouseClickTracker: Failed to create event tap";
            return false;
        }

        // Create run loop source
        m_runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, m_eventTap, 0);
        if (!m_runLoopSource) {
            qWarning() << "MouseClickTracker: Failed to create run loop source";
            CFRelease(m_eventTap);
            m_eventTap = nullptr;
            return false;
        }

        // Add to main run loop
        CFRunLoopAddSource(CFRunLoopGetMain(), m_runLoopSource, kCFRunLoopCommonModes);
        CGEventTapEnable(m_eventTap, true);

        m_running = true;
        qDebug() << "MouseClickTracker: Started tracking mouse clicks";
        return true;
    }

    void stop() override
    {
        if (!m_running) {
            return;
        }

        m_running = false;

        if (m_eventTap) {
            CGEventTapEnable(m_eventTap, false);
        }

        if (m_runLoopSource) {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), m_runLoopSource, kCFRunLoopCommonModes);
            CFRelease(m_runLoopSource);
            m_runLoopSource = nullptr;
        }

        if (m_eventTap) {
            CFRelease(m_eventTap);
            m_eventTap = nullptr;
        }

        qDebug() << "MouseClickTracker: Stopped tracking";
    }

    bool isRunning() const override
    {
        return m_running;
    }

private:
    static CGEventRef eventCallback(CGEventTapProxy proxy, CGEventType type,
                                    CGEventRef event, void *userData)
    {
        Q_UNUSED(proxy);

        MacMouseClickTracker *tracker = static_cast<MacMouseClickTracker*>(userData);
        if (!tracker || !tracker->m_running) {
            return event;
        }

        // Handle tap disabled events (system may disable tap under load)
        if (type == kCGEventTapDisabledByTimeout ||
            type == kCGEventTapDisabledByUserInput) {
            qDebug() << "MouseClickTracker: Event tap disabled, re-enabling...";
            if (tracker->m_eventTap) {
                CGEventTapEnable(tracker->m_eventTap, true);
            }
            return event;
        }

        // Get mouse position
        CGPoint location = CGEventGetLocation(event);
        QPoint pos(static_cast<int>(location.x), static_cast<int>(location.y));

        // Emit appropriate signal
        switch (type) {
        case kCGEventLeftMouseDown:
        case kCGEventRightMouseDown:
            QMetaObject::invokeMethod(tracker, [tracker, pos]() {
                emit tracker->mouseClicked(pos);
            }, Qt::QueuedConnection);
            break;

        case kCGEventLeftMouseUp:
        case kCGEventRightMouseUp:
            QMetaObject::invokeMethod(tracker, [tracker, pos]() {
                emit tracker->mouseReleased(pos);
            }, Qt::QueuedConnection);
            break;

        default:
            break;
        }

        return event;
    }

    CFMachPortRef m_eventTap;
    CFRunLoopSourceRef m_runLoopSource;
    bool m_running;
};

// Base class constructor
MouseClickTracker::MouseClickTracker(QObject *parent)
    : QObject(parent)
{
}

// Factory method
MouseClickTracker* MouseClickTracker::create(QObject *parent)
{
    return new MacMouseClickTracker(parent);
}
