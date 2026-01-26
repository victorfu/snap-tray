#ifndef SCROLLSENDER_H
#define SCROLLSENDER_H

#include <memory>
#include <QWindow>
#include "scrolling/ScrollingCaptureOptions.h"

/**
 * @brief Abstract interface for sending scroll events to a window
 *
 * Platform-specific implementations handle the actual scroll event generation.
 */
class ScrollSender
{
public:
    virtual ~ScrollSender() = default;

    /**
     * @brief Set the scroll method to use
     */
    virtual void setMethod(ScrollMethod method) = 0;

    /**
     * @brief Set the scroll amount (lines or wheel clicks)
     */
    virtual void setAmount(int amount) = 0;

    /**
     * @brief Send a single scroll event to the target window
     * @param window Target window ID
     * @return true if scroll event was sent successfully
     */
    virtual bool sendScroll(WId window) = 0;

    /**
     * @brief Scroll to the top of the content
     * @param window Target window ID
     * @return true if scroll-to-top was sent successfully
     */
    virtual bool scrollToTop(WId window) = 0;

    /**
     * @brief Check if the window has reached the bottom of scrollable content
     *
     * On Windows, uses GetScrollInfo API. On macOS, always returns false
     * (caller should use image comparison fallback).
     *
     * @param window Target window ID
     * @return true if at bottom, false if not or if detection not available
     */
    virtual bool isAtBottom(WId window) = 0;

    /**
     * @brief Set the target position for scroll events (macOS only)
     *
     * On macOS, CGEvent scroll events are sent to the cursor position.
     * This method sets the position where the cursor should be moved
     * before sending scroll events.
     *
     * @param x X coordinate (global screen coordinates)
     * @param y Y coordinate (global screen coordinates)
     */
    virtual void setTargetPosition(int x, int y) { Q_UNUSED(x); Q_UNUSED(y); }

    /**
     * @brief Factory method to create platform-specific implementation
     */
    static std::unique_ptr<ScrollSender> create();
};

#endif // SCROLLSENDER_H
