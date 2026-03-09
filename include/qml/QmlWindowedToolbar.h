#pragma once

#include <QObject>
#include <QRect>
#include <QPoint>
#include <QPointer>
#include <QElapsedTimer>

class QWidget;
class QQuickView;
class QQuickItem;
class PinToolbarViewModel;

namespace SnapTray {
class QmlFloatingSubToolbar;

/**
 * @brief QML-based floating toolbar for PinWindow.
 *
 * Drop-in replacement for WindowedToolbar (QWidget-based).
 * Uses QQuickView to render FloatingToolbar.qml with the same
 * glass-effect visual style as QmlRecordingControlBar.
 *
 * Pattern: identical to QmlRecordingControlBar —
 *   QmlOverlayManager::createScreenOverlay() for the QQuickView,
 *   PinToolbarViewModel as context property,
 *   QML MouseArea for drag, C++ repositions the QQuickView.
 */
class QmlWindowedToolbar : public QObject
{
    Q_OBJECT

public:
    explicit QmlWindowedToolbar(QObject* parent = nullptr);
    ~QmlWindowedToolbar() override;

    void show();
    void hide();
    void close();

    bool isVisible() const;

    void positionNear(const QRect& pinWindowRect);

    QRect geometry() const;

    PinToolbarViewModel* viewModel() const;

    // Associated widgets for click-outside detection
    void setAssociatedWidgets(QWidget* pinWindow, QmlFloatingSubToolbar* subToolbar);
    void setAssociatedTransientWidget(QWidget* widget);

signals:
    void closeRequested();
    void cursorRestoreRequested();
    void cursorSyncRequested();

private slots:
    void onButtonHovered(int buttonId, double anchorX, double anchorY,
                         double anchorW, double anchorH);
    void onButtonUnhovered();
    void onDragStarted();
    void onDragFinished();
    void onDragMoved(double deltaX, double deltaY);

private:
    void ensureView();
    void ensureTooltipView();
    void setupConnections();
    void applyPlatformWindowFlags();
    void applyTooltipWindowFlags();

    void showTooltip(const QString& text, const QRect& anchorRect);
    void hideTooltip();

    bool eventFilter(QObject* obj, QEvent* event) override;

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QQuickView* m_tooltipView = nullptr;
    QQuickItem* m_tooltipRootItem = nullptr;

    PinToolbarViewModel* m_viewModel = nullptr;

    // Drag state
    QPoint m_dragStartViewPos;
    QPoint m_dragStartCursorPos;
    bool m_isDragging = false;

    // Click-outside detection
    QElapsedTimer m_showTime;
    QWidget* m_associatedPinWindow = nullptr;
    QmlFloatingSubToolbar* m_associatedSubToolbar = nullptr;
    QPointer<QWidget> m_associatedTransientWidget;

    quint64 m_tooltipRequestId = 0;
};

} // namespace SnapTray
