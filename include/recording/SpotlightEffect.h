#ifndef SPOTLIGHTEFFECT_H
#define SPOTLIGHTEFFECT_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QColor>
#include <QTimer>

class QPainter;

/**
 * @brief Spotlight effect - highlights an area while dimming the rest.
 *
 * Creates a focused area that follows the cursor, with the surrounding
 * area dimmed to draw attention to the spotlight region.
 */
class SpotlightEffect : public QObject
{
    Q_OBJECT

public:
    enum class Mode {
        FollowCursor   // Circular spotlight follows cursor
        // FixedRegion mode deferred for future implementation
    };

    explicit SpotlightEffect(QObject *parent = nullptr);
    ~SpotlightEffect() override;

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

    // FollowCursor mode settings
    void setFollowRadius(int radius);
    int followRadius() const { return m_followRadius; }

    // Common settings
    void setDimOpacity(qreal opacity);  // Opacity of dimmed area (0.0-1.0)
    qreal dimOpacity() const { return m_dimOpacity; }

    void setFeatherRadius(int radius);  // Edge feather radius in pixels
    int featherRadius() const { return m_featherRadius; }

    /**
     * @brief Update cursor position (FollowCursor mode).
     */
    void updateCursorPosition(const QPoint &localPos);

    /**
     * @brief Render the spotlight effect.
     * @param painter Painter to draw with
     * @param recordingRegion Recording area bounds (in local coordinates)
     */
    void render(QPainter &painter, const QRect &recordingRegion) const;

    /**
     * @brief Check if there's anything to render.
     */
    bool hasVisibleContent() const;

signals:
    void needsRepaint();

private slots:
    void onUpdateTimer();

private:
    bool m_enabled = false;
    Mode m_mode = Mode::FollowCursor;

    // FollowCursor settings
    int m_followRadius = 100;
    QPointF m_cursorPos;
    QPointF m_smoothCursorPos;
    bool m_hasPosition = false;

    // Common settings
    qreal m_dimOpacity = 0.6;
    int m_featherRadius = 20;

    QTimer *m_updateTimer;

    static constexpr int kUpdateIntervalMs = 16;  // ~60fps
    static constexpr qreal kSmoothingFactor = 0.25;  // Smoothing coefficient
};

#endif // SPOTLIGHTEFFECT_H
