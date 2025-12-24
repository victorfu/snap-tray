#ifndef RECORDINGREGIONSELECTOR_H
#define RECORDINGREGIONSELECTOR_H

#include <QWidget>
#include <QRect>
#include <QPoint>

class QScreen;
class QPushButton;

class RecordingRegionSelector : public QWidget
{
    Q_OBJECT

public:
    explicit RecordingRegionSelector(QWidget *parent = nullptr);
    ~RecordingRegionSelector();

    void initializeForScreen(QScreen *screen);
    void initializeWithRegion(QScreen *screen, const QRect &region);

signals:
    void regionSelected(const QRect &region, QScreen *screen);
    void cancelled();
    void cancelledWithRegion(const QRect &region, QScreen *screen);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    // Handle positions for resizing
    enum class HandlePosition {
        None,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    void drawOverlay(QPainter &painter);
    void drawSelection(QPainter &painter);
    void drawCrosshair(QPainter &painter);
    void drawDimensionLabel(QPainter &painter);
    void drawInstructions(QPainter &painter);
    void finishSelection();
    void setupButtons();
    void positionButtons();

    // Handle hit testing and cursor
    HandlePosition hitTestHandle(const QPoint &pos) const;
    QRect handleRect(HandlePosition handle) const;
    void updateCursor(HandlePosition handle);

    QScreen *m_currentScreen;
    qreal m_devicePixelRatio;

    QPoint m_startPoint;
    QPoint m_currentPoint;
    QRect m_selectionRect;
    bool m_isSelecting;
    bool m_selectionComplete;

    // Handle dragging state
    bool m_isDraggingHandle;
    HandlePosition m_activeHandle;
    QPoint m_dragStartPos;
    QRect m_dragStartRect;

    // Buttons for selection confirmation
    QWidget *m_buttonContainer;
    QPushButton *m_startButton;
    QPushButton *m_cancelButton;

    static const int HANDLE_SIZE = 10;
};

#endif // RECORDINGREGIONSELECTOR_H
