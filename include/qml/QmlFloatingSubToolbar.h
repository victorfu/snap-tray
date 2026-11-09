#pragma once

#include "qml/ToolbarTooltipController.h"

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>

class QQuickView;
class QQuickItem;
class PinToolOptionsViewModel;
class QWidget;
class QWindow;
class TestQmlFloatingSubToolbarTooltip;

namespace SnapTray {

/**
 * @brief QML-based floating sub-toolbar overlay for tool options.
 *
 * Renders ToolOptionsStrip.qml with glass-effect styling.
 * Used by RegionSelector, ScreenCanvas (with external ViewModel),
 * and PinWindow (with internally-owned ViewModel).
 */
class QmlFloatingSubToolbar : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct with an external ViewModel. Ownership is NOT transferred.
     */
    explicit QmlFloatingSubToolbar(PinToolOptionsViewModel* viewModel,
                                   QObject* parent = nullptr);

    /**
     * @brief Construct with an internally-owned ViewModel (created automatically).
     */
    explicit QmlFloatingSubToolbar(QObject* parent = nullptr);

    ~QmlFloatingSubToolbar() override;

    void show();
    void hide();
    void close();

    bool isVisible() const;

    void positionBelow(const QRect& toolbarRect);

    QRect geometry() const;
    QWindow* window() const;

    PinToolOptionsViewModel* viewModel() const;

    /**
     * @brief Show tool options for the given tool.
     * Delegates to ViewModel::showForTool(). Hides if tool has no options.
     */
    void showForTool(int toolId);

    /**
     * @brief Set the parent widget for coordinate mapping.
     * When set, the NSWindow level is adjusted to parent+1.
     * When not set, NSFloatingWindowLevel is used.
     */
    void setParentWidget(QWidget* parent);

signals:
    void emojiPickerRequested();

private slots:
    // ToolOptionsStrip.qml asks for a tooltip on hover; controlName is the
    // hovered control's objectName.
    void onControlTooltipRequested(const QString& controlName, const QString& text);
    void onControlTooltipDismissed(const QString& controlName);

private:
    friend class ::TestQmlFloatingSubToolbarTooltip;

    void ensureView();
    void applyPlatformWindowFlags();
    void syncTransientParent();
    void syncCursorSurface();
    void cancelParentCursorRestore();
    void scheduleParentCursorRestore();
    void attemptParentCursorRestore(quint64 token, int remainingAttempts);
    bool eventFilter(QObject* obj, QEvent* event) override;
    QQuickItem* visibleControl(const QString& controlName) const;
    QRect controlGlobalRect(const QString& controlName) const;
    void showControlTooltip();
    void hideControlTooltip();
    void setAutoBlurHintEmphasis(bool active);

    PinToolOptionsViewModel* m_viewModel = nullptr;
    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QWidget* m_parentWidget = nullptr;
    quint64 m_parentCursorRestoreToken = 0;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
    ToolbarTooltipController m_tooltip;
    TooltipPlacement m_tooltipPlacement = TooltipPlacement::Below;
    // The control whose tooltip is showing; null when none is.
    QPointer<QQuickItem> m_tooltipControl;
    QString m_tooltipText;
    QMetaObject::Connection m_tooltipControlVisibility;
};

} // namespace SnapTray
