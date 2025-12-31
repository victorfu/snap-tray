#ifndef MOUSECLICKTRACKER_H
#define MOUSECLICKTRACKER_H

#include <QObject>
#include <QPoint>

/**
 * @brief Abstract interface for tracking global mouse clicks.
 *
 * This class provides a platform-independent interface for monitoring
 * mouse clicks anywhere on the screen. It is used to display click
 * highlight effects during screen recording.
 *
 * Platform implementations:
 * - macOS: Uses CGEvent tap (requires Accessibility permission)
 * - Windows: Uses low-level mouse hook (WH_MOUSE_LL)
 */
class MouseClickTracker : public QObject
{
    Q_OBJECT

public:
    explicit MouseClickTracker(QObject *parent = nullptr);
    ~MouseClickTracker() override = default;

    /**
     * @brief Start tracking mouse clicks.
     * @return true if tracking started successfully
     */
    virtual bool start() = 0;

    /**
     * @brief Stop tracking mouse clicks.
     */
    virtual void stop() = 0;

    /**
     * @brief Check if currently tracking.
     */
    virtual bool isRunning() const = 0;

    /**
     * @brief Factory method to create platform-specific tracker.
     * @param parent Parent QObject
     * @return New MouseClickTracker instance, or nullptr if unavailable
     */
    static MouseClickTracker* create(QObject *parent = nullptr);

signals:
    /**
     * @brief Emitted when a mouse button is pressed.
     * @param globalPos Click position in screen coordinates
     */
    void mouseClicked(QPoint globalPos);

    /**
     * @brief Emitted when a mouse button is released.
     * @param globalPos Release position in screen coordinates
     */
    void mouseReleased(QPoint globalPos);
};

#endif // MOUSECLICKTRACKER_H
