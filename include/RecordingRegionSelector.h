#ifndef RECORDINGREGIONSELECTOR_H
#define RECORDINGREGIONSELECTOR_H

#include <QWidget>
#include <QRect>
#include <QPoint>

class QScreen;

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
    void leaveEvent(QEvent *event) override;

private:
    enum ButtonId {
        ButtonNone = -1,
        ButtonStart = 0,
        ButtonCancel = 1
    };

    void drawOverlay(QPainter &painter);
    void drawSelection(QPainter &painter);
    void drawCrosshair(QPainter &painter);
    void drawDimensionLabel(QPainter &painter);
    void drawInstructions(QPainter &painter);
    void drawToolbar(QPainter &painter);
    void drawTooltip(QPainter &painter);
    void finishSelection();
    void setupIcons();
    void updateButtonRects();
    int buttonAtPosition(const QPoint &pos) const;
    QString tooltipForButton(ButtonId button) const;
    void handleCancel();

    QScreen *m_currentScreen;
    qreal m_devicePixelRatio;

    QPoint m_startPoint;
    QPoint m_currentPoint;
    QRect m_selectionRect;
    bool m_isSelecting;
    bool m_selectionComplete;

    // Toolbar
    QRect m_toolbarRect;
    QRect m_startRect;
    QRect m_cancelRect;
    int m_hoveredButton;

    // Layout constants (matching ToolbarWidget)
    static const int TOOLBAR_HEIGHT = 32;
    static const int BUTTON_WIDTH = 28;
    static const int BUTTON_SPACING = 2;
};

#endif // RECORDINGREGIONSELECTOR_H
