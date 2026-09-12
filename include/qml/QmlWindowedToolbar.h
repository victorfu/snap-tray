#pragma once

#include <QObject>
#include <QRect>
#include <QPoint>
#include <QPointer>
#include <QElapsedTimer>
#include <QString>

#include "cursor/CursorTypes.h"

class QWidget;
class QWindow;
class QQuickView;
class QQuickItem;
class PinToolbarViewModel;
class QScreen;

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
    QWindow* window() const;
    QWindow* tooltipWindow() const;
    QWindow* overflowWindow() const;

    PinToolbarViewModel* viewModel() const;

    // Associated widgets for click-outside detection
    void setAssociatedWidgets(QWidget* pinWindow, QmlFloatingSubToolbar* subToolbar);
    void setAssociatedTransientWidget(QWidget* widget);
    void setAssociatedTransientWindow(QWindow* window);

signals:
    void closeRequested();

private slots:
    void onButtonHovered(int buttonId, double anchorX, double anchorY,
                         double anchorW, double anchorH);
    void onButtonUnhovered();
    void onDragStarted();
    void onDragFinished();
    void onDragMoved(double deltaX, double deltaY);
    void onOverflowRequested(double anchorX, double anchorY, double anchorW, double anchorH);
    void onOverflowActionTriggered(int buttonId);

private:
    friend class TestToolbarOverflow;
    void ensureView();
    void ensureTooltipView();
    void setupConnections();
    void syncTransientParent();
    void syncTooltipTransientParent();
    void applyPlatformWindowFlags();
    void applyTooltipWindowFlags();
    void syncCursorSurface(const CursorStyleSpec* explicitStyle = nullptr);

    void showTooltip(const QString& text, const QRect& anchorRect);
    void hideTooltip();
    void hideOverflow();
    void updateOverflowLayout(QScreen* screen);
    void applyOverflowLayout(const QRect& usableBounds);
    void refreshScreenGeometry();
    void positionOverflow();

    bool eventFilter(QObject* obj, QEvent* event) override;

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QQuickView* m_tooltipView = nullptr;
    QQuickItem* m_tooltipRootItem = nullptr;
    QQuickView* m_overflowView = nullptr;
    QQuickItem* m_overflowRootItem = nullptr;
    QString m_overflowCursorSurfaceId;
    QPointer<QScreen> m_layoutScreen;
    QList<QMetaObject::Connection> m_screenConnections;
    QRect m_lastPinWindowRect;
    QRect m_overflowAnchor;
    bool m_userPositioned = false;
    bool m_updatingLayout = false;

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
    QPointer<QWindow> m_associatedTransientWindow;

    quint64 m_tooltipRequestId = 0;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};

} // namespace SnapTray
