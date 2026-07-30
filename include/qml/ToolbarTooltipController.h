#pragma once

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>

class QQuickItem;
class QQuickView;
class QWidget;
class QWindow;

namespace SnapTray {

enum class TooltipPlacement
{
    Above,
    Below,
};

/**
 * @brief Input-transparent tooltip positioned around a global anchor rect.
 *
 * The controller owns a lightweight QML overlay using RecordingTooltip.qml.
 * It prefers the requested side of the anchor, flips when that side would be
 * off-screen, and clamps the final geometry to the anchor's screen.
 */
class ToolbarTooltipController : public QObject
{
    Q_OBJECT

public:
    explicit ToolbarTooltipController(QObject* parent = nullptr);
    ~ToolbarTooltipController() override;

    void showFor(const QString& text,
                 const QRect& anchorGlobalRect,
                 QQuickView* owner,
                 TooltipPlacement preferred);
    void hide();

    /**
     * @brief Keep the tooltip above this widget on platforms with window levels.
     *
     * The guarded pointer deliberately does not extend the widget's lifetime.
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
