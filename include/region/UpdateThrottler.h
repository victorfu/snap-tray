#ifndef UPDATETHROTTLER_H
#define UPDATETHROTTLER_H

#include <QElapsedTimer>

/**
 * @brief UpdateThrottler provides per-operation throttling for UI updates.
 *
 * Different operations have different update frequency requirements:
 * - Selection: 120fps (8ms) - needs to feel instant
 * - Annotation: ~80fps (12ms) - smooth drawing
 * - Hover effects: ~30fps (32ms) - less critical
 * - Magnifier: ~60fps (16ms) - smooth but not critical
 */
class UpdateThrottler
{
public:
    enum class ThrottleType {
        Selection,
        Annotation,
        Hover,
        Magnifier
    };

    UpdateThrottler();

    /**
     * @brief Check if enough time has passed for an update of the given type.
     * @param type The type of update to check
     * @return true if update should proceed, false to skip
     */
    bool shouldUpdate(ThrottleType type) const;

    /**
     * @brief Reset the timer for the given type after an update.
     * @param type The type of update that was performed
     */
    void reset(ThrottleType type);

    /**
     * @brief Start all timers. Call once during initialization.
     */
    void startAll();

    // Throttle intervals in milliseconds
    static constexpr int kSelectionMs = 8;    // 120fps for selection
    static constexpr int kAnnotationMs = 12;  // ~80fps for drawing
    static constexpr int kHoverMs = 32;       // ~30fps for hover effects
    static constexpr int kMagnifierMs = 16;   // ~60fps for magnifier

private:
    int getInterval(ThrottleType type) const;
    QElapsedTimer& getTimer(ThrottleType type);
    const QElapsedTimer& getTimer(ThrottleType type) const;

    QElapsedTimer m_selectionTimer;
    QElapsedTimer m_annotationTimer;
    QElapsedTimer m_hoverTimer;
    QElapsedTimer m_magnifierTimer;
};

#endif // UPDATETHROTTLER_H
