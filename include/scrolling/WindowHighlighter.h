#ifndef WINDOWHIGHLIGHTER_H
#define WINDOWHIGHLIGHTER_H

#include <QWidget>
#include <QRect>
#include <QTimer>
#include <QPointer>

class WindowDetector;
class QScreen;

/**
 * @brief Full-screen transparent overlay for window selection with embedded controls
 *
 * Tracks cursor position, detects window under cursor via WindowDetector,
 * draws highlight border around the detected window with Start/Cancel buttons
 * embedded within the highlight area.
 */
class WindowHighlighter : public QWidget
{
    Q_OBJECT

public:
    explicit WindowHighlighter(QWidget *parent = nullptr);
    ~WindowHighlighter();

    /**
     * @brief Set the window detector to use for detection
     */
    void setWindowDetector(WindowDetector* detector);

    /**
     * @brief Start tracking cursor position and highlighting windows
     */
    void startTracking();

    /**
     * @brief Stop tracking and hide overlay
     */
    void stopTracking();

    /**
     * @brief Set whether capture is in progress (changes border style)
     */
    void setCapturing(bool capturing);

    /**
     * @brief Get the currently highlighted window rect
     */
    QRect highlightedRect() const { return m_highlightRect; }

    /**
     * @brief Get the currently highlighted window ID
     */
    WId highlightedWindow() const { return m_highlightedWindow; }

    /**
     * @brief Get the window title
     */
    QString windowTitle() const { return m_windowTitle; }

signals:
    /**
     * @brief Emitted when user clicks Start button
     */
    void startClicked();

    /**
     * @brief Emitted when user clicks Cancel button or presses Escape
     */
    void cancelled();

    /**
     * @brief Emitted when highlighted window changes
     */
    void windowHovered(WId windowId, const QString& title);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void updateHighlight();
    void updateAnimation();

private:
    void drawHighlightBorder(QPainter& painter);
    void drawAnimatedBorder(QPainter& painter);
    void drawDimOverlay(QPainter& painter);
    void drawControlBar(QPainter& painter);
    void drawButton(QPainter& painter, const QRect& rect, const QString& text,
                    bool isPrimary, bool isHovered);
    void updateButtonRects();
    int buttonAtPosition(const QPoint& pos) const;
    QRect mapToLocal(const QRect& rect) const;

    WindowDetector* m_detector = nullptr;
    QTimer* m_updateTimer = nullptr;
    QTimer* m_animationTimer = nullptr;
    QPointer<QScreen> m_currentScreen;

    QRect m_highlightRect;
    WId m_highlightedWindow = 0;
    QString m_windowTitle;

    qreal m_animationOffset = 0.0;
    bool m_isCapturing = false;
    bool m_isTracking = false;

    // Embedded control bar
    QRect m_controlBarRect;
    QRect m_startButtonRect;
    QRect m_cancelButtonRect;
    int m_hoveredButton = 0;  // 0=none, 1=start, 2=cancel

    // Button constants
    static constexpr int kButtonNone = 0;
    static constexpr int kButtonStart = 1;
    static constexpr int kButtonCancel = 2;

    // Styling
    static constexpr int kBorderWidth = 3;
    static constexpr int kDashLength = 10;
    static constexpr int kUpdateIntervalMs = 16;  // ~60fps
    static constexpr int kControlBarHeight = 44;
    static constexpr int kButtonWidth = 80;
    static constexpr int kButtonHeight = 32;
    static constexpr int kButtonSpacing = 12;
    static constexpr int kControlBarPadding = 8;
};

#endif // WINDOWHIGHLIGHTER_H
