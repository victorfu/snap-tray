#include "GlassRenderer.h"

#include <QLinearGradient>
#include <QPainterPath>
#include <algorithm>

void GlassRenderer::drawGlassPanel(QPainter& painter,
                                   const QRect& rect,
                                   const ToolbarStyleConfig& config,
                                   int radiusOverride)
{
    int radius = (radiusOverride >= 0) ? radiusOverride : config.cornerRadius;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 2. Draw glass background with gradient simulation
    drawGlassBackground(painter, rect, config, radius);

    // 3. Draw hairline border
    drawHairlineBorder(painter, rect, config, radius);

    // 4. Draw inner highlight for depth
    drawInnerHighlight(painter, rect, config, radius);

    painter.restore();
}

void GlassRenderer::drawGlassShadow(QPainter& painter,
                                    const QRect& rect,
                                    const ToolbarStyleConfig& config,
                                    int radius)
{
    painter.setPen(Qt::NoPen);

    // Multi-layer shadow for soft macOS look
    // Each layer is progressively larger and more transparent
    for (int i = 4; i >= 1; --i) {
        QColor shadowColor = config.shadowColor;
        shadowColor.setAlpha(shadowColor.alpha() / i);

        // Shadow extends downward more than sideways (macOS floating style)
        QRect shadowRect = rect.adjusted(-i, config.shadowOffsetY, i, config.shadowOffsetY + i * 2);

        painter.setBrush(shadowColor);
        painter.drawRoundedRect(shadowRect, radius + i, radius + i);
    }
}

void GlassRenderer::drawGlassBackground(QPainter& painter,
                                        const QRect& rect,
                                        const ToolbarStyleConfig& config,
                                        int radius)
{
    // Create vertical gradient for glass simulation
    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());

    // Top color is slightly brighter for light refraction effect
    QColor topColor = config.glassBackgroundColor;
    topColor.setAlpha(std::min(255, topColor.alpha() + 20));

    gradient.setColorAt(0, topColor);
    gradient.setColorAt(1, config.glassBackgroundColor);

    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(rect, radius, radius);
}

void GlassRenderer::drawHairlineBorder(QPainter& painter,
                                       const QRect& rect,
                                       const ToolbarStyleConfig& config,
                                       int radius)
{
    painter.setPen(QPen(config.hairlineBorderColor, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect, radius, radius);
}

void GlassRenderer::drawInnerHighlight(QPainter& painter,
                                       const QRect& rect,
                                       const ToolbarStyleConfig& config,
                                       int radius)
{
    // Skip if highlight color is fully transparent
    if (config.glassHighlightColor.alpha() == 0) {
        return;
    }

    // Create a clipping path for the top portion of the rect
    QPainterPath clipPath;
    clipPath.addRoundedRect(rect.adjusted(1, 1, -1, -rect.height() / 2), radius - 1, radius - 1);

    // Create gradient for inner highlight (fades from top to transparent)
    QLinearGradient highlightGradient(rect.topLeft(), QPoint(rect.left(), rect.top() + 8));
    highlightGradient.setColorAt(0, config.glassHighlightColor);
    highlightGradient.setColorAt(1, Qt::transparent);

    painter.save();
    painter.setClipPath(clipPath);
    painter.setPen(Qt::NoPen);
    painter.setBrush(highlightGradient);
    painter.drawRect(rect);
    painter.restore();
}
