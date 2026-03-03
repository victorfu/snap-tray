#pragma once

#include <QObject>
#include <QRect>
#include <QString>
#include <QPointer>

class QQuickView;
class QQuickItem;
class QWidget;

namespace SnapTray {

/**
 * @brief QML-based toast notification, drop-in replacement for UnifiedToast.
 *
 * Loads Toast.qml via QmlOverlayManager and exposes the same public API
 * as UnifiedToast so callers can migrate with minimal changes.
 *
 * Usage:
 *   // Screen-level (replaces UnifiedToast::screenToast()):
 *   QmlToast::screenToast().showToast(Level::Success, "Saved", "File saved to ~/Desktop");
 *
 *   // Parent-anchored:
 *   auto* toast = new QmlToast(parentWidget);
 *   toast->showToast(Level::Success, "Copied to clipboard");
 *
 *   // Near selection:
 *   auto* toast = new QmlToast(this);
 *   toast->showNearRect(Level::Error, "No text found", selectionRect);
 */
class QmlToast : public QObject
{
    Q_OBJECT

public:
    enum class Level { Success = 0, Error = 1, Warning = 2, Info = 3 };

    enum class AnchorMode {
        ScreenTopRight,    ///< Floating top-level window, top-right of screen
        ParentTopCenter,   ///< Centered at top of parent widget
        NearRect           ///< Centered above a given QRect
    };

    /**
     * @brief Get the screen-level toast singleton.
     */
    static QmlToast& screenToast();

    /**
     * @brief Create a parent-anchored toast.
     * @param parent The parent widget
     * @param shadowMargin Margin offset from parent edges
     */
    explicit QmlToast(QWidget* parent, int shadowMargin = 8);

    ~QmlToast() override;

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

private:
    /// Private constructor for screen-level singleton
    QmlToast();

    void ensureView();
    void setQmlProperties(Level level, const QString& title, const QString& message, int durationMs);
    void positionAndShow();
    void positionScreenTopRight();
    void positionParentTopCenter();
    void positionNearRect();

    // State
    AnchorMode m_anchorMode = AnchorMode::ScreenTopRight;
    AnchorMode m_defaultAnchorMode = AnchorMode::ScreenTopRight;
    int m_shadowMargin = 8;
    QRect m_anchorRect;
    bool m_isScreenLevel = false;

    // QML view
    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QPointer<QWidget> m_parentWidget;

    // Layout constants (matching UnifiedToast)
    static constexpr int kScreenMargin = 20;
};

} // namespace SnapTray
