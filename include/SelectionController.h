#ifndef SELECTIONCONTROLLER_H
#define SELECTIONCONTROLLER_H

#include <QObject>
#include <QRect>
#include <QPoint>
#include <Qt>

/**
 * @brief Manages selection rectangle state with resize handles and move.
 */
class SelectionController : public QObject
{
    Q_OBJECT

public:
    enum class Handle {
        None = -1,
        TopLeft, Top, TopRight,
        Left, Center, Right,
        BottomLeft, Bottom, BottomRight
    };

    explicit SelectionController(QObject* parent = nullptr);

    /**
     * @brief Set the selection rectangle.
     */
    void setSelectionRect(const QRect& rect);
    QRect selectionRect() const { return m_selectionRect; }

    /**
     * @brief Check if selection is complete.
     */
    bool isComplete() const { return m_isComplete; }
    void setComplete(bool complete);

    /**
     * @brief Get handle at position.
     */
    Handle handleAtPosition(const QPoint& pos) const;

    /**
     * @brief Start resizing from a handle.
     */
    void startResize(Handle handle, const QPoint& pos);

    /**
     * @brief Update resize to new position.
     */
    void updateResize(const QPoint& pos);

    /**
     * @brief Finish resizing.
     */
    void finishResize();

    /**
     * @brief Cancel resizing.
     */
    void cancelResize();

    /**
     * @brief Check if currently resizing.
     */
    bool isResizing() const { return m_isResizing; }

    /**
     * @brief Check if currently moving.
     */
    bool isMoving() const { return m_isMoving; }

    /**
     * @brief Get cursor shape for a handle.
     */
    Qt::CursorShape cursorForHandle(Handle handle) const;

    /**
     * @brief Set viewport bounds for clamping.
     */
    void setViewportRect(const QRect& rect);

    /**
     * @brief Set minimum selection size.
     */
    void setMinimumSize(const QSize& size);

signals:
    void selectionChanged(const QRect& rect);
    void resizeStarted();
    void resizeFinished();

private:
    QRect m_selectionRect;
    QRect m_originalRect;
    QRect m_viewportRect;
    QSize m_minimumSize;

    bool m_isComplete;
    bool m_isResizing;
    bool m_isMoving;
    Handle m_activeHandle;
    QPoint m_startPoint;

    static const int HANDLE_SIZE = 16;  // Hit area
};

#endif // SELECTIONCONTROLLER_H
