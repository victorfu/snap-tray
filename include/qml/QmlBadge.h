#pragma once

#include <QWidget>
#include <QString>

class QQuickWidget;
class QQuickItem;

namespace SnapTray {

/**
 * @brief QML-based badge for transient indicators (zoom, opacity).
 *
 * Drop-in replacement for OverlayBadge. Uses QQuickWidget to embed
 * Badge.qml as a child of a parent QWidget.
 *
 * Usage:
 *   auto* badge = new QmlBadge(parentWidget);
 *   badge->showBadge("125%");           // show with default 1500ms duration
 *   badge->showBadge("80%", 2000);      // custom duration
 */
class QmlBadge : public QWidget {
    Q_OBJECT

public:
    explicit QmlBadge(QWidget* parent);
    ~QmlBadge() override = default;

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

private slots:
    void onQmlHidden();

private:
    void ensureInitialized();

    QQuickWidget* m_quickWidget = nullptr;
    QQuickItem* m_rootItem = nullptr;
    bool m_initialized = false;
    bool m_logicallyVisible = false;
};

} // namespace SnapTray
