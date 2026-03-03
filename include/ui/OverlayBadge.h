#ifndef OVERLAYBADGE_H
#define OVERLAYBADGE_H

#include <QWidget>

class QTimer;
class QPropertyAnimation;

namespace SnapTray {

/**
 * @brief Lightweight glass-effect badge for transient indicators (zoom, opacity).
 *
 * Shares the same glass visual language as UnifiedToast but is smaller,
 * single-line, centered text, no icon, no accent strip.
 *
 * Usage:
 *   auto* badge = new OverlayBadge(parentWidget);
 *   badge->showBadge("125%");           // show with default 1500ms duration
 *   badge->showBadge("80%", 2000);      // custom duration
 */
class OverlayBadge : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal fadeOpacity READ fadeOpacity WRITE setFadeOpacity)

public:
    explicit OverlayBadge(QWidget* parent);
    ~OverlayBadge() override = default;

    /**
     * @brief Show the badge with text, auto-hides after durationMs.
     *
     * If already visible, updates text and restarts the timer (no re-fade-in).
     */
    void showBadge(const QString& text, int durationMs = 1500);

    /**
     * @brief Immediately hide (with fade-out animation).
     */
    void hideBadge();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    qreal fadeOpacity() const { return m_fadeOpacity; }
    void setFadeOpacity(qreal opacity);

    void startFadeIn();
    void startFadeOut();

    QString m_text;
    qreal m_fadeOpacity = 0.0;
    bool m_visible = false;   // tracks logical visibility (distinct from QWidget::isVisible)

    QTimer* m_hideTimer = nullptr;
    QPropertyAnimation* m_fadeInAnim = nullptr;
    QPropertyAnimation* m_fadeOutAnim = nullptr;

    // Layout constants
    static constexpr int kPaddingH = 10;
    static constexpr int kPaddingV = 5;
    static constexpr int kCornerRadius = 6;
    static constexpr int kFontSize = 12;
    static constexpr int kFadeInMs = 150;
    static constexpr int kFadeOutMs = 200;
};

} // namespace SnapTray

#endif // OVERLAYBADGE_H
