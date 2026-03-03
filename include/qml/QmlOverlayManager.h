#pragma once

#include <QObject>
#include <QQuickView>
#include <QPointer>
#include <QUrl>
#include <QWidget>

class QQmlEngine;

namespace SnapTray {

/**
 * @brief Singleton managing QML overlay windows and embedded widgets.
 *
 * Creates QQuickView-based frameless, transparent windows for screen-level
 * overlays (e.g., screen toast) and provides factory methods for creating
 * QQuickView instances that can be parented to QWidgets.
 *
 * The shared QML engine has the SnapTrayQml module registered so that
 * QML components can access PrimitiveTokens, SemanticTokens, ComponentTokens,
 * and ThemeManager.
 */
class QmlOverlayManager : public QObject
{
    Q_OBJECT

public:
    static QmlOverlayManager& instance();

    /**
     * @brief Get the shared QML engine with SnapTrayQml types registered.
     */
    QQmlEngine* engine() const;

    /**
     * @brief Create a QQuickView for a screen-level overlay (frameless, transparent, stay-on-top).
     * @param qmlUrl URL of the QML component to load (e.g., "qrc:/SnapTrayQml/components/Toast.qml")
     * @return Owned QQuickView pointer; caller takes ownership.
     */
    QQuickView* createScreenOverlay(const QUrl& qmlUrl);

    /**
     * @brief Create a QQuickView for a parent-anchored overlay.
     * The view is frameless and transparent, positioned manually by the caller.
     * @param qmlUrl URL of the QML component to load
     * @param parent The parent widget (view is not reparented but caller should manage lifetime)
     * @return Owned QQuickView pointer; caller takes ownership.
     */
    QQuickView* createParentOverlay(const QUrl& qmlUrl, QWidget* parent = nullptr);

private:
    explicit QmlOverlayManager(QObject* parent = nullptr);
    QmlOverlayManager(const QmlOverlayManager&) = delete;
    QmlOverlayManager& operator=(const QmlOverlayManager&) = delete;

    void ensureEngine();

    QQmlEngine* m_engine = nullptr;
};

} // namespace SnapTray
