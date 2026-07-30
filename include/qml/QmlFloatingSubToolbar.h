#pragma once

#include "qml/ToolbarTooltipController.h"

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QTimer>

class QQuickView;
class QQuickItem;
class PinToolOptionsViewModel;
class QWidget;
class QWindow;
class TestQmlFloatingSubToolbarMosaicHint;

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
    void onActiveToolChanged();
    void onMosaicBrushAdjustmentLearned();
    void onMosaicBrushPreviewHovered(double globalX,
                                      double globalY,
                                      double width,
                                      double height);
    void onMosaicBrushPreviewHoverExited();

private:
    friend class ::TestQmlFloatingSubToolbarMosaicHint;

    enum class MosaicHintDisplay
    {
        None,
        Coachmark,
        HoverReminder,
    };

    void ensureView();
    void initializeHintConnections();
    void applyPlatformWindowFlags();
    void syncTransientParent();
    void syncCursorSurface();
    void cancelParentCursorRestore();
    void scheduleParentCursorRestore();
    void attemptParentCursorRestore(quint64 token, int remainingAttempts);
    bool eventFilter(QObject* obj, QEvent* event) override;
    void activateMosaicHint();
    void deactivateMosaicHint();
    void suspendMosaicHint();
    void showPendingMosaicCoachmark();
    void showMosaicHint(MosaicHintDisplay display, const QRect& anchorGlobalRect);
    void hideMosaicHint();
    void setMosaicHintEmphasis(bool active);
    QRect mosaicPreviewGlobalRect() const;

    PinToolOptionsViewModel* m_viewModel = nullptr;
    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QPointer<QWidget> m_parentWidget;
    quint64 m_parentCursorRestoreToken = 0;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
    ToolbarTooltipController m_tooltip;
    QTimer m_mosaicCoachmarkTimer;
    MosaicHintDisplay m_mosaicHintDisplay = MosaicHintDisplay::None;
    TooltipPlacement m_mosaicHintPlacement = TooltipPlacement::Below;
    bool m_mosaicHintActivationActive = false;
    bool m_mosaicCoachmarkPending = false;
    bool m_mosaicBrushAdjustmentLearned = false;
    bool m_mosaicPreviewHovered = false;
    bool m_suppressHoverReminderUntilExit = false;
};

} // namespace SnapTray
