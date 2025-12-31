#include "toolbar/ToolbarRenderer.h"
#include "ToolbarStyle.h"
#include "GlassRenderer.h"

#include <QPainter>
#include <QFontMetrics>

namespace Toolbar {

int ToolbarRenderer::calculateWidth(const QVector<ButtonConfig>& buttons)
{
    int separatorCount = 0;
    for (const auto& btn : buttons) {
        if (btn.separatorBefore) {
            separatorCount++;
        }
    }

    return buttons.size() * (BUTTON_WIDTH + BUTTON_SPACING)
           + 2 * HORIZONTAL_PADDING
           + separatorCount * SEPARATOR_WIDTH;
}

QVector<QRect> ToolbarRenderer::calculateButtonRects(const QRect& toolbarRect,
                                                     const QVector<ButtonConfig>& buttons)
{
    QVector<QRect> rects(buttons.size());

    if (buttons.isEmpty()) {
        return rects;
    }

    int x = toolbarRect.left() + HORIZONTAL_PADDING;
    int y = toolbarRect.top() + (TOOLBAR_HEIGHT - BUTTON_WIDTH + 4) / 2;

    for (int i = 0; i < buttons.size(); ++i) {
        // Add separator space
        if (buttons[i].separatorBefore) {
            x += SEPARATOR_WIDTH;
        }

        rects[i] = QRect(x, y, BUTTON_WIDTH, BUTTON_WIDTH - 4);
        x += BUTTON_WIDTH + BUTTON_SPACING;
    }

    return rects;
}

void ToolbarRenderer::drawSeparator(QPainter& painter, int x, int y, int height,
                                    const ToolbarStyleConfig& style)
{
    painter.setPen(style.separatorColor);
    painter.drawLine(x, y, x, y + height);
}

void ToolbarRenderer::drawTooltip(QPainter& painter, const QRect& buttonRect,
                                  const QString& tooltip, const ToolbarStyleConfig& style,
                                  bool above, int viewportWidth)
{
    if (tooltip.isEmpty()) {
        return;
    }

    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(tooltip);
    textRect.adjust(-8, -4, 8, 4);

    // Position tooltip above or below the button
    int tooltipX = buttonRect.center().x() - textRect.width() / 2;
    int tooltipY;
    if (above) {
        tooltipY = buttonRect.top() - textRect.height() - 6;
    } else {
        tooltipY = buttonRect.bottom() + 6;
    }

    // Keep on screen horizontally
    if (tooltipX < 5) {
        tooltipX = 5;
    }
    if (viewportWidth > 0 && tooltipX + textRect.width() > viewportWidth - 5) {
        tooltipX = viewportWidth - textRect.width() - 5;
    }

    textRect.moveTo(tooltipX, tooltipY);

    // Draw glass tooltip without shadow
    ToolbarStyleConfig tooltipConfig = style;
    tooltipConfig.shadowColor.setAlpha(0);
    tooltipConfig.shadowOffsetY = 0;
    tooltipConfig.shadowBlurRadius = 0;
    GlassRenderer::drawGlassPanel(painter, textRect, tooltipConfig, 6);

    // Draw tooltip text
    painter.setPen(style.tooltipText);
    painter.drawText(textRect, Qt::AlignCenter, tooltip);
}

int ToolbarRenderer::buttonAtPosition(const QPoint& pos, const QVector<QRect>& buttonRects)
{
    for (int i = 0; i < buttonRects.size(); ++i) {
        if (buttonRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

QRect ToolbarRenderer::positionBelow(const QRect& referenceRect, int toolbarWidth,
                                     int viewportWidth, int viewportHeight,
                                     int margin)
{
    QRect sel = referenceRect.normalized();

    // Position below selection, right-aligned
    int toolbarX = sel.right() - toolbarWidth + 1;
    int toolbarY = sel.bottom() + margin;

    // If toolbar would go off screen, position inside selection (top-right corner)
    if (toolbarY + TOOLBAR_HEIGHT > viewportHeight - 5) {
        toolbarY = sel.top() + margin;
    }

    // Keep on screen horizontally
    if (toolbarX < 5) {
        toolbarX = 5;
    }
    if (viewportWidth > 0 && toolbarX + toolbarWidth > viewportWidth - 5) {
        toolbarX = viewportWidth - toolbarWidth - 5;
    }

    return QRect(toolbarX, toolbarY, toolbarWidth, TOOLBAR_HEIGHT);
}

QRect ToolbarRenderer::positionAtBottom(int centerX, int bottomY, int toolbarWidth)
{
    int toolbarX = centerX - toolbarWidth / 2;
    int toolbarY = bottomY - TOOLBAR_HEIGHT;

    return QRect(toolbarX, toolbarY, toolbarWidth, TOOLBAR_HEIGHT);
}

QColor ToolbarRenderer::getIconColor(const ButtonConfig& config, bool isActive,
                                     bool isHovered, const ToolbarStyleConfig& style)
{
    Q_UNUSED(isHovered)

    if (isActive) {
        return style.iconActiveColor;
    }
    if (config.isCancel) {
        return style.iconCancelColor;
    }
    if (config.isRecord) {
        return style.iconRecordColor;
    }
    if (config.isAction) {
        return style.iconActionColor;
    }
    return style.iconNormalColor;
}

} // namespace Toolbar
