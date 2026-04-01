#pragma once

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QUrl>
#include <QString>
#include <memory>

class QQuickView;
class QQuickItem;
class QWidget;
class QEvent;

namespace SnapTray {

struct QmlOverlayPanelOptions {
    bool transparentForInput = false;
    bool acceptsFocus = false;
};

/**
 * @brief Generic QML overlay window wrapper for non-interactive or lightly-interactive panels.
 *
 * Follows the QmlFloatingToolbar pattern but stripped of toolbar-specific logic
 * (no drag, no tooltips, no cursor sync). Used for capture overlay panels
 * (shortcut hints, region control, multi-region list).
 */
class QmlOverlayPanel : public QObject
{
    Q_OBJECT

public:
    using Options = QmlOverlayPanelOptions;

    QmlOverlayPanel(const QUrl& qmlSource,
                    QObject* viewModel,
                    const QString& contextPropertyName,
                    const Options& options = {},
                    QObject* parent = nullptr);
    ~QmlOverlayPanel() override;

    void show();
    void hide();
    void close();
    bool isVisible() const;
    QRect geometry() const;
    QRect anchorRect() const;
    bool containsGlobalPoint(const QPoint& globalPos) const;
    void setPosition(const QPoint& globalPos);
    void resize(const QSize& size);
    void setParentWidget(QWidget* parent);
    QQuickView* view() const;

private:
    void ensureView();
    void applyPlatformWindowFlags();
    void syncTransientParent();
    void updateWindowMask();
    void syncCursorSurface();
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onWindowMaskRectsChanged();

private:
    QUrl m_qmlSource;
    QObject* m_viewModel = nullptr;
    QString m_contextPropertyName;
    Options m_options;

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QWidget* m_parentWidget = nullptr;
    QRegion m_windowMask;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};

} // namespace SnapTray
