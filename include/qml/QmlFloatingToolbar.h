#pragma once

#include <QObject>
#include <QRect>
#include <QPoint>
#include <QElapsedTimer>
#include <QList>
#include <QWindow>
#include <QColor>
#include <QString>

#include "cursor/CursorTypes.h"

class QQuickView;
class QQuickItem;
class QWidget;
class QWindow;

namespace SnapTray {

/**
 * @brief Generic QML-based floating toolbar overlay.
 *
 * Creates a frameless, transparent QQuickView window that renders
 * FloatingToolbar.qml. Used by RegionSelector and ScreenCanvas as the
 * shared floating toolbar overlay for full-screen capture surfaces.
 *
 * Unlike QmlWindowedToolbar (PinWindow), this class does NOT implement
 * click-outside detection. The full-screen host widget handles all input
 * management.
 *
 * Pattern: QmlOverlayManager::createScreenOverlay() for the QQuickView,
 * any QObject* ViewModel as context property, QML renders autonomously.
 */
class QmlFloatingToolbar : public QObject
{
    Q_OBJECT

public:
    enum class HorizontalAlignment
    {
        Center,
        RightEdge,
    };

    enum class IconPalette
    {
        Default = 0,
        CaptureOverlay,
    };

    struct Appearance
    {
        IconPalette iconPalette = IconPalette::Default;
        QColor iconNormalColor;
        QColor iconActionColor;
        QColor iconCancelColor;
        QColor iconActiveColor;
    };

    /**
     * @param viewModel The toolbar ViewModel (RegionToolbarViewModel or CanvasToolbarViewModel).
     *                  Must have the properties/slots expected by FloatingToolbar.qml.
     *                  Ownership is NOT transferred.
     */
    explicit QmlFloatingToolbar(QObject* viewModel, QObject* parent = nullptr);
    QmlFloatingToolbar(QObject* viewModel, const Appearance& appearance, QObject* parent = nullptr);
    ~QmlFloatingToolbar() override;

    void show();
    void hide();
    void close();

    bool isVisible() const;

    /**
     * @brief Position toolbar below a selection rect with viewport boundary checks.
     */
    void positionForSelection(const QRect& selectionRect,
                              int viewportWidth, int viewportHeight,
                              HorizontalAlignment alignment = HorizontalAlignment::Center);

    /**
     * @brief Position toolbar at a specific point (centered horizontally).
     */
    void positionAt(int centerX, int bottomY);

    /**
     * @brief Move the toolbar to a screen position.
     */
    void setPosition(const QPoint& pos);
    void setDragBounds(const QList<QRect>& bounds);
    void clearDragBounds();
    static QPoint clampTopLeftToBounds(const QPoint& desiredTopLeft,
                                       const QSize& windowSize,
                                       const QList<QRect>& bounds);

    QRect geometry() const;
    int width() const;
    int height() const;
    QWindow* window() const;
    QWindow* tooltipWindow() const;

    /**
     * @brief Set the parent widget to keep the toolbar above it.
     */
    void setParentWidget(QWidget* parent);

signals:
    void dragStarted();
    void dragFinished();
    void dragMoved(double deltaX, double deltaY);

private slots:
    void onDragStarted();
    void onDragFinished();
    void onDragMoved(double deltaX, double deltaY);
    void onButtonHovered(int buttonId, double anchorX, double anchorY,
                         double anchorW, double anchorH);
    void onButtonUnhovered();

private:
    void ensureView();
    void ensureTooltipView();
    void setupConnections();
    void applyAppearance();
    void applyPlatformWindowFlags();
    void applyTooltipWindowFlags();
    void syncCursorSurface(const CursorStyleSpec* explicitStyle = nullptr);

    void showTooltip(const QString& text, const QRect& anchorRect);
    void hideTooltip();

    bool eventFilter(QObject* obj, QEvent* event) override;

    QObject* m_viewModel = nullptr;
    Appearance m_appearance;
    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QQuickView* m_tooltipView = nullptr;
    QQuickItem* m_tooltipRootItem = nullptr;
    QWidget* m_parentWidget = nullptr;
    QList<QRect> m_dragBounds;

    // Drag state
    QPoint m_dragStartViewPos;
    QPoint m_dragStartCursorPos;
    bool m_isDragging = false;

    quint64 m_tooltipRequestId = 0;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};

} // namespace SnapTray
