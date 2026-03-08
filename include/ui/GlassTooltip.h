#ifndef GLASSTOOLTIP_H
#define GLASSTOOLTIP_H

#include <QWidget>
#include "ToolbarStyle.h"

namespace SnapTray {

/**
 * @brief Reusable glass-effect tooltip widget.
 *
 * Reusable glass-effect tooltip. Used by QWidget-based toolbars.
 * QML-based toolbars (QmlWindowedToolbar) use their own QML tooltip views.
 *
 * Usage:
 *   auto* tip = new GlassTooltip(nullptr);
 *   tip->show("Save image", style, anchorGlobalPos);
 */
class GlassTooltip : public QWidget {
    Q_OBJECT

public:
    explicit GlassTooltip(QWidget* parent = nullptr);
    ~GlassTooltip() override = default;

    /**
     * @brief Show tooltip with text at a position.
     * @param text Tooltip text
     * @param style Toolbar style for glass rendering
     * @param globalPos Global position to anchor tooltip (X = center, Y = edge)
     * @param above If true, show above globalPos; otherwise below
     * @param showShadow If true, render drop shadow (Recording bar needs it)
     */
    void show(const QString& text, const ToolbarStyleConfig& style,
              const QPoint& globalPos, bool above = true, bool showShadow = false);

    void hideTooltip();

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_text;
    ToolbarStyleConfig m_style;
    bool m_showShadow = false;
};

} // namespace SnapTray

#endif // GLASSTOOLTIP_H
