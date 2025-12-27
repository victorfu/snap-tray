#ifndef SCROLLINGCAPTURETOOLBAR_H
#define SCROLLINGCAPTURETOOLBAR_H

#include <QWidget>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QString>

class QLabel;

class ScrollingCaptureToolbar : public QWidget
{
    Q_OBJECT

public:
    enum class Mode {
        Adjusting,  // Direction, Start, Cancel
        Capturing,  // Size label, Stop, Cancel
        Finished    // Pin, Save, Copy, Close
    };
    Q_ENUM(Mode)

    explicit ScrollingCaptureToolbar(QWidget *parent = nullptr);
    ~ScrollingCaptureToolbar();

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

    void positionNear(const QRect &region);
    void updateSize(int width, int height);
    void setMatchStatus(bool matched, double confidence);

signals:
    void startClicked();
    void stopClicked();
    void pinClicked();
    void saveClicked();
    void copyClicked();
    void closeClicked();
    void cancelClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    enum class ButtonId {
        Start,
        Stop,
        Pin,
        Save,
        Copy,
        Close,
        Cancel
    };

    struct ButtonConfig {
        ButtonId id;
        QString iconKey;
        QString tooltip;
        bool isAction;   // Blue action button
        bool isCancel;   // Red cancel button
    };

    void setupUi();
    void loadIcons();
    void updateButtonLayout();
    int buttonAtPosition(const QPoint &pos) const;
    void drawButton(QPainter &painter, int index);
    void drawTooltip(QPainter &painter);

    Mode m_mode = Mode::Adjusting;

    // Labels
    QLabel *m_sizeLabel;
    QLabel *m_statusIndicator;

    // Button configuration
    QVector<ButtonConfig> m_buttons;
    QVector<QRect> m_buttonRects;
    QVector<bool> m_buttonVisible;
    int m_hoveredButton = -1;

    // Drag support
    QPoint m_dragStartPos;
    QPoint m_dragStartWidgetPos;
    bool m_isDragging = false;

    static constexpr int TOOLBAR_HEIGHT = 36;
    static constexpr int BUTTON_SIZE = 24;
    static constexpr int BUTTON_SPACING = 4;
    static constexpr int MARGIN = 8;
};

#endif // SCROLLINGCAPTURETOOLBAR_H
