#ifndef UNIFIEDTOAST_H
#define UNIFIEDTOAST_H

#include <QWidget>
#include <QTimer>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>

namespace SnapTray {

/**
 * @brief Unified toast notification component.
 *
 * Replaces GlobalToast, UIIndicators::showToast(), and RegionSelector::showSelectionToast()
 * with a single component supporting three anchor modes and glass-effect rendering.
 *
 * Usage:
 *   // Screen-level (replaces GlobalToast):
 *   UnifiedToast::screenToast().showToast(Level::Success, "Saved", "File saved to ~/Desktop");
 *
 *   // Parent-anchored (replaces UIIndicators::showToast):
 *   auto* toast = new UnifiedToast(parentWidget);
 *   toast->showToast(Level::Success, "Copied to clipboard");
 *
 *   // Near selection (replaces RegionSelector::showSelectionToast):
 *   auto* toast = new UnifiedToast(this);
 *   toast->showNearRect(Level::Error, "No text found", selectionRect);
 */
class UnifiedToast : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal fadeOpacity READ fadeOpacity WRITE setFadeOpacity)
    Q_PROPERTY(int slideOffset READ slideOffset WRITE setSlideOffset)

public:
    enum class Level { Success, Error, Warning, Info };

    enum class AnchorMode {
        ScreenTopRight,    ///< Floating top-level window, top-right of screen
        ParentTopCenter,   ///< Child widget, centered at top of parent
        NearRect           ///< Child widget, centered above a given QRect
    };

    /**
     * @brief Get the screen-level toast singleton (replaces GlobalToast).
     */
    static UnifiedToast& screenToast();

    /**
     * @brief Create a parent-anchored toast.
     * @param parent The parent widget (toast is a child)
     * @param shadowMargin Margin offset from parent edges
     */
    explicit UnifiedToast(QWidget* parent, int shadowMargin = 8);

    ~UnifiedToast() override;

    /**
     * @brief Show a toast message.
     */
    void showToast(Level level, const QString& title,
                   const QString& message = QString(), int durationMs = 2500);

    /**
     * @brief Show a toast anchored near a specific rect.
     */
    void showNearRect(Level level, const QString& title,
                      const QRect& anchorRect, int durationMs = 2500);

    void setShadowMargin(int margin) { m_shadowMargin = margin; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// Private constructor for screen-level singleton
    UnifiedToast();

    void setupAnimations();
    void displayCurrent();

    // Rendering
    void paintToast(QPainter& painter);
    QColor accentColorForLevel(Level level) const;
    QString iconKeyForLevel(Level level) const;
    void ensureIconsLoaded();

    // Sizing & positioning
    QSize calculateToastSize() const;
    void positionToast();

    // Animation
    void animateIn();
    void animateOut();
    void onHideFinished();

    // Q_PROPERTY accessors
    qreal fadeOpacity() const { return m_fadeOpacity; }
    void setFadeOpacity(qreal opacity);
    int slideOffset() const { return m_slideOffset; }
    void setSlideOffset(int offset);

    // State
    AnchorMode m_anchorMode = AnchorMode::ScreenTopRight;
    AnchorMode m_defaultAnchorMode = AnchorMode::ScreenTopRight;
    int m_shadowMargin = 8;

    Level m_currentLevel = Level::Info;
    QString m_currentTitle;
    QString m_currentMessage;
    QRect m_anchorRect;

    // Animation properties
    qreal m_fadeOpacity = 0.0;
    int m_slideOffset = 0;

    // Animation objects
    QParallelAnimationGroup* m_showGroup = nullptr;
    QParallelAnimationGroup* m_hideGroup = nullptr;
    QTimer* m_displayTimer = nullptr;
    bool m_iconsLoaded = false;

    // Layout constants
    static constexpr int kAccentStripWidth = 4;
    static constexpr int kCornerRadius = 8;
    static constexpr int kPaddingH = 14;
    static constexpr int kPaddingV = 10;
    static constexpr int kIconSize = 18;
    static constexpr int kIconTextGap = 8;
    static constexpr int kScreenToastWidth = 320;
    static constexpr int kTitleFontSize = 13;
    static constexpr int kMessageFontSize = 12;
    static constexpr int kScreenMargin = 20;
    static constexpr int kTitleMessageGap = 4;
};

} // namespace SnapTray

#endif // UNIFIEDTOAST_H
