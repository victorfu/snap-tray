#pragma once

#include <QObject>
#include <QRect>
#include <QPoint>
#include <QElapsedTimer>

class QQuickView;
class QQuickItem;
class PinToolOptionsViewModel;
class QEvent;

namespace SnapTray {

/**
 * @brief QML-based floating sub-toolbar for PinWindow tool options.
 *
 * Drop-in replacement for WindowedSubToolbar (QWidget-based).
 * Renders ToolOptionsStrip.qml with glass-effect styling.
 *
 * Pattern: same as QmlWindowedToolbar / QmlRecordingControlBar —
 *   QmlOverlayManager::createScreenOverlay() for QQuickView,
 *   PinToolOptionsViewModel as context property.
 */
class QmlWindowedSubToolbar : public QObject
{
    Q_OBJECT

public:
    explicit QmlWindowedSubToolbar(QObject* parent = nullptr);
    ~QmlWindowedSubToolbar() override;

    void show();
    void hide();
    void close();

    bool isVisible() const;

    void positionBelow(const QRect& toolbarRect);

    QRect geometry() const;

    PinToolOptionsViewModel* viewModel() const;

    /**
     * @brief Show tool options for the given tool.
     * Delegates to ViewModel::showForTool(). Hides if tool has no options.
     */
    void showForTool(int toolId);

signals:
    void emojiPickerRequested();
    void cursorRestoreRequested();
    void cursorSyncRequested();

private:
    void ensureView();
    void applyPlatformWindowFlags();
    bool eventFilter(QObject* obj, QEvent* event) override;

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;

    PinToolOptionsViewModel* m_viewModel = nullptr;
};

} // namespace SnapTray
