#include "scrolling/ScrollSender.h"
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

/**
 * @brief macOS implementation of ScrollSender
 *
 * Uses CGEvent API to send scroll wheel events.
 * macOS scroll events are POSITION-BASED - they go to the window under the cursor.
 * The cursor should be positioned at the target before calling sendScroll().
 *
 * Note: macOS doesn't have reliable APIs to detect scroll position,
 * so isAtBottom() always returns false (caller uses image comparison).
 */
class MacScrollSender : public ScrollSender
{
public:
    void setMethod(ScrollMethod method) override
    {
        m_method = method;
    }

    void setAmount(int amount) override
    {
        m_amount = amount;
    }

    void setTargetPosition(int x, int y) override
    {
        m_targetCenterX = x;
        m_targetCenterY = y;
        m_targetCenterValid = true;
    }

    bool sendScroll(WId window) override
    {
        Q_UNUSED(window);  // macOS scroll is position-based, not window-based

        CGEventRef event = nullptr;

        switch (m_method) {
        case ScrollMethod::MouseWheel:
        case ScrollMethod::SendMessage:  // Fall back to mouse wheel on macOS
            // Create scroll wheel event
            // Negative values = scroll down (content moves up)
            event = CGEventCreateScrollWheelEvent(
                nullptr,
                kCGScrollEventUnitLine,
                1,
                -m_amount
            );
            break;

        case ScrollMethod::KeyDown:
            // Send Down arrow key presses
            for (int i = 0; i < m_amount; i++) {
                sendKeyPress(kVK_DownArrow, false);
            }
            return true;

        case ScrollMethod::PageDown:
            // Send Page Down key
            sendKeyPress(kVK_PageDown, false);
            return true;
        }

        if (!event) {
            return false;
        }

        // Set explicit event location to target center
        // This ensures scroll goes to the right window even if cursor moved slightly
        if (m_targetCenterValid) {
            CGEventSetLocation(event, CGPointMake(m_targetCenterX, m_targetCenterY));
        }

        // Post to HID event tap - event goes to window under the specified location
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        return true;
    }

    bool scrollToTop(WId window) override
    {
        Q_UNUSED(window);

        // Send Cmd+Up to scroll to top (works in most apps)
        sendKeyPress(kVK_UpArrow, true);

        // Also try Home key
        sendKeyPress(kVK_Home, false);

        return true;
    }

    bool isAtBottom(WId window) override
    {
        Q_UNUSED(window);
        // macOS doesn't have a reliable API for this
        // Caller should use image comparison fallback
        return false;
    }

private:
    void sendKeyPress(CGKeyCode keyCode, bool withCommand)
    {
        CGEventFlags flags = withCommand ? kCGEventFlagMaskCommand : 0;

        CGEventRef keyDown = CGEventCreateKeyboardEvent(nullptr, keyCode, true);
        CGEventRef keyUp = CGEventCreateKeyboardEvent(nullptr, keyCode, false);

        if (keyDown && keyUp) {
            CGEventSetFlags(keyDown, flags);
            CGEventSetFlags(keyUp, flags);

            CGEventPost(kCGHIDEventTap, keyDown);
            CGEventPost(kCGHIDEventTap, keyUp);
        }

        if (keyDown) CFRelease(keyDown);
        if (keyUp) CFRelease(keyUp);
    }

    ScrollMethod m_method = ScrollMethod::MouseWheel;
    int m_amount = 2;
    int m_targetCenterX = 0;
    int m_targetCenterY = 0;
    bool m_targetCenterValid = false;
};

std::unique_ptr<ScrollSender> ScrollSender::create()
{
    return std::make_unique<MacScrollSender>();
}
