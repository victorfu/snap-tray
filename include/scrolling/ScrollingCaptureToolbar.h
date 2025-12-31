#ifndef SCROLLINGCAPTURETOOLBAR_H
#define SCROLLINGCAPTURETOOLBAR_H

#include <QWidget>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QString>
#include "toolbar/ToolbarButtonConfig.h"

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

    enum class Direction {
        Vertical,
        Horizontal
    };
    Q_ENUM(Direction)

    explicit ScrollingCaptureToolbar(QWidget *parent = nullptr);
    ~ScrollingCaptureToolbar();

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

    void setDirection(Direction direction);
    Direction direction() const { return m_direction; }

    void positionNear(const QRect &region);
    void updateSize(int width, int height);
    void setMatchStatus(bool matched, double confidence);
    void setMatchRecoveryInfo(int lastSuccessfulPos, bool showRecoveryHint);

signals:
    void startClicked();
    void stopClicked();
    void pinClicked();
    void saveClicked();
    void copyClicked();
    void closeClicked();
    void cancelClicked();
    void directionToggled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // Button IDs specific to ScrollingCaptureToolbar
    enum ButtonId {
        ButtonDirection = 0,  // Toggle vertical/horizontal
        ButtonStart,
        ButtonStop,
        ButtonPin,
        ButtonSave,
        ButtonCopy,
        ButtonClose,
        ButtonCancel
    };

    // Use shared ButtonConfig
    using ButtonConfig = Toolbar::ButtonConfig;

    void setupUi();
    void loadIcons();
    void updateButtonLayout();
    int buttonAtPosition(const QPoint &pos) const;
    void drawButton(QPainter &painter, int index);
    void drawTooltip(QPainter &painter);

    Mode m_mode = Mode::Adjusting;
    Direction m_direction = Direction::Vertical;
    bool m_showRecoveryHint = false;
    int m_lastSuccessfulPos = 0;

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
