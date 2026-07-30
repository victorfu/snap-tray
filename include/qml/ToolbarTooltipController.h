#pragma once

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>

class QQuickView;
class QQuickItem;
class QWidget;
class QWindow;

namespace SnapTray {

enum class TooltipPlacement
{
    Above,
    Below,
};

/**
 * @brief Shared hover tooltip for QML toolbar overlays.
 *
 * Owns a transparent-for-input QQuickView running RecordingTooltip.qml,
 * positioned relative to an anchor rect in global coordinates. Used by the
 * capture toolbar, the Pin Window toolbar, and the tool options sub-toolbar.
 */
class ToolbarTooltipController : public QObject
{
    Q_OBJECT

public:
    explicit ToolbarTooltipController(QObject* parent = nullptr);
    ~ToolbarTooltipController() override;

    /**
     * @brief Show the tooltip near an anchor.
     * @param text Tooltip text; an empty string hides instead.
     * @param anchorGlobalRect Anchor rect in global screen coordinates.
     * @param owner The toolbar view; becomes the transient parent.
     * @param preferred Side to try first. Flips to the other side when the
     *        preferred side would leave the screen.
     */
    void showFor(const QString& text,
                 const QRect& anchorGlobalRect,
                 QQuickView* owner,
                 TooltipPlacement preferred);

    void hide();

    /**
     * @brief Extra window whose level the tooltip must stay above (macOS).
     *
     * Set by the Pin Window toolbar so the tooltip is not covered by the pin
     * window. Leave unset elsewhere.
     */
    void setAssociatedWidget(QWidget* widget);

    QWindow* window() const;

private:
    void ensureView();
    void applyWindowFlags(QQuickView* owner);

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QPointer<QWidget> m_associatedWidget;
    quint64 m_requestId = 0;
};

} // namespace SnapTray
