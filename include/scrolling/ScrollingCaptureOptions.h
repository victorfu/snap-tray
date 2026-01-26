#ifndef SCROLLINGCAPTUREOPTIONS_H
#define SCROLLINGCAPTUREOPTIONS_H

#include <QObject>
#include <QtGlobal>

/**
 * @brief Scroll method for sending scroll events to target window
 */
enum class ScrollMethod {
    MouseWheel,     // Mouse wheel events (most universal) - default
    SendMessage,    // Windows WM_VSCROLL messages
    KeyDown,        // Arrow key down presses
    PageDown        // Page Down key press
};
Q_DECLARE_METATYPE(ScrollMethod)

/**
 * @brief Capture result status (mirrors ShareX CaptureStatus)
 */
enum class CaptureStatus {
    Successful,           // All frames had reliable matches
    PartiallySuccessful,  // Some frames used fallback matching
    Failed                // Could not find valid matches
};
Q_DECLARE_METATYPE(CaptureStatus)

/**
 * @brief State machine states for scrolling capture
 */
enum class ScrollingCaptureState {
    Idle,           // No capture activity
    Selecting,      // Mouse hover -> window highlight, waiting for Start click
    Capturing,      // Auto-scrolling and capturing frames
    Processing,     // Combining frames into final image
    Preview         // Showing preview with Save/Copy/Pin/Retry options
};
Q_DECLARE_METATYPE(ScrollingCaptureState)

/**
 * @brief Configuration options for scrolling capture
 *
 * Default values are conservative for stability, following ShareX patterns.
 */
struct ScrollingCaptureOptions {
    // Timing control
    int startDelayMs = 500;       // Delay before first capture (allow capture engine to initialize)
    int scrollDelayMs = 500;      // Delay after each scroll (wait for content to settle)

    // Scrolling control
    ScrollMethod scrollMethod = ScrollMethod::MouseWheel;
    int scrollAmount = 2;         // Lines or wheel clicks per scroll
    bool autoScrollTop = false;   // Scroll to top before starting

    // Matching control
    bool autoIgnoreBottomEdge = true;   // Auto-detect sticky footer
    int minMatchLines = 10;             // Minimum matching lines threshold

    // Safety limits
    int maxFrames = 50;           // Maximum frames to capture
    int maxHeightPx = 20000;      // Maximum result image height

    /**
     * @brief Compute side margin for line comparison
     *
     * Ignores scrollbar and floating element areas on edges.
     * For narrow images (width < 150), returns a small margin to preserve comparison area.
     * For normal images, uses width/20, capped between 50 and width/3.
     *
     * @param width Image width in pixels
     * @return Side margin in pixels
     */
    static int computeSideMargin(int width) {
        // For very narrow images, use smaller margin to preserve comparison area
        if (width < 150) {
            return qMax(10, width / 10);
        }
        // For normal images: width/20, capped between 50 and width/3
        return qBound(50, width / 20, width / 3);
    }

    /**
     * @brief Compute match search limit
     *
     * Search range is half of the smaller image height.
     *
     * @param resultHeight Height of accumulated result image
     * @param currentHeight Height of current frame
     * @return Maximum search range in pixels
     */
    static int computeMatchLimit(int resultHeight, int currentHeight) {
        return qMin(resultHeight, currentHeight) / 2;
    }
};

#endif // SCROLLINGCAPTUREOPTIONS_H
