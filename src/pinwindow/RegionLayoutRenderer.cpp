#include "pinwindow/RegionLayoutRenderer.h"
#include "pinwindow/RegionLayoutManager.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QFontMetrics>

void RegionLayoutRenderer::drawRegions(QPainter& painter,
                                       const QVector<LayoutRegion>& regions,
                                       int selectedIndex,
                                       int hoveredIndex,
                                       qreal dpr)
{
    Q_UNUSED(dpr)

    painter.save();

    // Draw non-selected regions first
    for (int i = 0; i < regions.size(); ++i) {
        if (i == selectedIndex) {
            continue;  // Draw selected region last (on top)
        }
        bool isHovered = (i == hoveredIndex);
        drawRegionBorder(painter, regions[i], false, isHovered);
        drawIndexBadge(painter, regions[i].rect, regions[i].index, regions[i].color);
    }

    // Draw selected region on top
    if (selectedIndex >= 0 && selectedIndex < regions.size()) {
        const auto& selectedRegion = regions[selectedIndex];
        bool isHovered = (selectedIndex == hoveredIndex);
        drawRegionBorder(painter, selectedRegion, true, isHovered);
        drawResizeHandles(painter, selectedRegion.rect, selectedRegion.color);
        drawIndexBadge(painter, selectedRegion.rect, selectedRegion.index, selectedRegion.color);
    }

    painter.restore();
}

void RegionLayoutRenderer::drawRegionBorder(QPainter& painter,
                                            const LayoutRegion& region,
                                            bool isSelected,
                                            bool isHovered)
{
    painter.save();

    QColor borderColor = region.color;

    // Lighten color for hover effect
    if (isHovered && !isSelected) {
        borderColor = borderColor.lighter(130);
    }

    int borderWidth = isSelected ? LayoutModeConstants::kSelectedBorderWidth
                                 : LayoutModeConstants::kBorderWidth;

    QPen pen(borderColor, borderWidth);
    pen.setStyle(isSelected ? Qt::SolidLine : Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Draw border slightly outside the region
    QRect borderRect = region.rect.adjusted(-1, -1, 1, 1);
    painter.drawRect(borderRect);

    painter.restore();
}

void RegionLayoutRenderer::drawResizeHandles(QPainter& painter,
                                             const QRect& rect,
                                             const QColor& color)
{
    painter.save();

    const int hs = LayoutModeConstants::kHandleSize;
    const int halfHs = hs / 2;

    // Handle positions: corners and edge midpoints
    QVector<QPoint> handlePositions = {
        QPoint(rect.left(), rect.top()),                    // TopLeft
        QPoint(rect.center().x(), rect.top()),              // Top
        QPoint(rect.right(), rect.top()),                   // TopRight
        QPoint(rect.right(), rect.center().y()),            // Right
        QPoint(rect.right(), rect.bottom()),                // BottomRight
        QPoint(rect.center().x(), rect.bottom()),           // Bottom
        QPoint(rect.left(), rect.bottom()),                 // BottomLeft
        QPoint(rect.left(), rect.center().y())              // Left
    };

    // Draw handles
    painter.setPen(QPen(color, 1));
    painter.setBrush(Qt::white);

    for (const QPoint& pos : handlePositions) {
        QRect handleRect(pos.x() - halfHs, pos.y() - halfHs, hs, hs);
        painter.drawRect(handleRect);
    }

    painter.restore();
}

void RegionLayoutRenderer::drawIndexBadge(QPainter& painter,
                                          const QRect& rect,
                                          int index,
                                          const QColor& color)
{
    painter.save();

    const int badgeSize = LayoutModeConstants::kBadgeSize;
    const int margin = 4;

    // Position badge at top-left corner of region
    QRect badgeRect(rect.left() + margin,
                    rect.top() + margin,
                    badgeSize,
                    badgeSize);

    // Draw circular badge background
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(badgeRect);

    // Draw index number
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPixelSize(LayoutModeConstants::kBadgeFontSize);
    font.setBold(true);
    painter.setFont(font);

    QString text = QString::number(index);
    painter.drawText(badgeRect, Qt::AlignCenter, text);

    painter.restore();
}

void RegionLayoutRenderer::drawConfirmCancelButtons(QPainter& painter,
                                                    const QRect& canvasBounds,
                                                    const QPoint& hoverPos)
{
    painter.save();

    auto styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

    QRect confirmRect = confirmButtonRect(canvasBounds);
    QRect cancelRect = cancelButtonRect(canvasBounds);

    bool confirmHovered = confirmRect.contains(hoverPos);
    bool cancelHovered = cancelRect.contains(hoverPos);

    // Draw button backgrounds using glass style
    painter.setRenderHint(QPainter::Antialiasing);

    // Confirm button
    {
        QColor bgColor = confirmHovered ? QColor(76, 175, 80, 230)   // Green hover
                                        : QColor(76, 175, 80, 200);  // Green normal
        painter.setPen(Qt::NoPen);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(confirmRect, 6, 6);

        // Draw text
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPixelSize(14);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(confirmRect, Qt::AlignCenter, QStringLiteral("Confirm"));
    }

    // Cancel button
    {
        QColor bgColor = cancelHovered ? QColor(158, 158, 158, 230)   // Gray hover
                                       : QColor(158, 158, 158, 200);  // Gray normal
        painter.setPen(Qt::NoPen);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(cancelRect, 6, 6);

        // Draw text
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPixelSize(14);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(cancelRect, Qt::AlignCenter, QStringLiteral("Cancel"));
    }

    painter.restore();
}

QRect RegionLayoutRenderer::confirmButtonRect(const QRect& canvasBounds)
{
    const int buttonWidth = LayoutModeConstants::kButtonWidth;
    const int buttonHeight = LayoutModeConstants::kButtonHeight;
    const int spacing = LayoutModeConstants::kButtonSpacing;
    const int margin = LayoutModeConstants::kButtonMargin;

    // Position buttons at bottom center of canvas
    int totalWidth = buttonWidth * 2 + spacing;
    int x = canvasBounds.center().x() - totalWidth / 2;
    int y = canvasBounds.bottom() - margin - buttonHeight;

    return QRect(x, y, buttonWidth, buttonHeight);
}

QRect RegionLayoutRenderer::cancelButtonRect(const QRect& canvasBounds)
{
    QRect confirmRect = confirmButtonRect(canvasBounds);
    const int spacing = LayoutModeConstants::kButtonSpacing;

    return QRect(confirmRect.right() + spacing,
                 confirmRect.top(),
                 LayoutModeConstants::kButtonWidth,
                 LayoutModeConstants::kButtonHeight);
}
