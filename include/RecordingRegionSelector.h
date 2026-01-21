#ifndef RECORDINGREGIONSELECTOR_H
#define RECORDINGREGIONSELECTOR_H

#include <QWidget>
#include <QRect>
#include <QPoint>
#include <QPointer>

class QScreen;
class ToolbarCore;

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
    void finishSelection();
    void setupIcons();
    void setupToolbar();
    void updateToolbarPosition();
    void handleCancel();

    QPointer<QScreen> m_currentScreen;
    qreal m_devicePixelRatio;

    QPoint m_startPoint;
    QPoint m_currentPoint;
    QRect m_selectionRect;
    bool m_isSelecting;
    bool m_selectionComplete;

    // Toolbar using ToolbarCore
    ToolbarCore *m_toolbar;
};

#endif // RECORDINGREGIONSELECTOR_H
