#include "region/RegionPainter.h"
#include "annotation/AnnotationRenderHelper.h"
#include "region/SelectionStateManager.h"
#include "region/MultiRegionManager.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/TextBoxAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "tools/ToolManager.h"
#include "tools/ToolId.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include "TransformationGizmo.h"
#include "tools/ToolTraits.h"
#include "utils/CoordinateHelper.h"

#include <QPainter>
#include <QPainterPath>
#include <QWidget>
#include <QDebug>
#include <QtMath>

namespace {

constexpr qreal kSelectionBorderWidth = 2.0;
constexpr int kSelectionHandleDiameter = 8;
constexpr int kSelectionHandleRadius = kSelectionHandleDiameter / 2;
constexpr int kSelectionChromeMargin = kSelectionHandleRadius + 1;
constexpr int kDimensionPanelHeight = 28;
constexpr int kDimensionPanelPadding = 24;
constexpr int kDimensionPanelTopGap = 8;
constexpr int kDimensionPanelInset = 5;
const QColor kRegionDimColor(0, 0, 0, 100);

}

RegionPainter::RegionPainter(QObject* parent)
    : QObject(parent)
{
}

void RegionPainter::setSelectionManager(SelectionStateManager* manager)
{
    m_selectionManager = manager;
}

void RegionPainter::setAnnotationLayer(AnnotationLayer* layer)
{
    m_annotationLayer = layer;
}

void RegionPainter::setToolManager(ToolManager* manager)
{
    m_toolManager = manager;
}



void RegionPainter::setParentWidget(QWidget* widget)
{
    m_parentWidget = widget;
}

void RegionPainter::setMultiRegionManager(MultiRegionManager* manager)
{
    m_multiRegionManager = manager;
}

void RegionPainter::setHighlightedWindowRect(const QRect& rect)
{
    m_highlightedWindowRect = rect;
}

void RegionPainter::setCornerRadius(int radius)
{
    m_cornerRadius = radius;
}

void RegionPainter::setShowSubToolbar(bool show)
{
    m_showSubToolbar = show;
}

void RegionPainter::setCurrentTool(int tool)
{
    m_currentTool = tool;
}

void RegionPainter::setDevicePixelRatio(qreal ratio)
{
    m_devicePixelRatio = ratio;
}

void RegionPainter::setReplacePreview(int targetIndex, const QRect& previewRect)
{
    m_replaceTargetIndex = targetIndex;
    m_replacePreviewRect = previewRect;
}

void RegionPainter::paint(QPainter& painter, const QPixmap& background, const QRegion& dirtyRegion)
{
    if (!m_parentWidget || !m_selectionManager) {
        return;
    }

    const QRegion updateRegion = dirtyRegion.isEmpty()
        ? QRegion(m_parentWidget->rect())
        : dirtyRegion;
    const QRect updateBounds = updateRegion.boundingRect();
    const QRect selectionRect = m_selectionManager->selectionRect();
    const bool selectionPreviewOwnedByOverlay =
        m_selectionPreviewActive && m_selectionManager->isSelecting();
    const bool singleRegionFastPath =
        !selectionPreviewOwnedByOverlay && !m_multiRegionMode && !background.isNull();

    if (singleRegionFastPath) {
        ensureDimmedBackgroundCache(background);
        drawBackgroundTiles(painter, m_dimmedBackgroundCache, updateRegion);

        QRegion clearRegion;
        if (m_selectionManager->hasActiveSelection() && selectionRect.isValid()) {
            clearRegion = updateRegion.intersected(QRegion(selectionRect.normalized()));
        } else if (!m_highlightedWindowRect.isNull()) {
            clearRegion = updateRegion.intersected(QRegion(m_highlightedWindowRect));
        }

        if (!clearRegion.isEmpty()) {
            drawBackgroundTiles(painter, background, clearRegion);
        }
    }
    else if (!background.isNull()) {
        const qreal dpr = background.devicePixelRatio();
#ifdef Q_OS_WIN
#ifndef QT_NO_DEBUG
        const qreal roundedDpr = qRound(dpr);
        const bool fractionalDpr = !qFuzzyCompare(dpr, roundedDpr);
        const bool partialRepaint =
            !dirtyRegion.isEmpty() && updateBounds != m_parentWidget->rect();
        if (fractionalDpr && partialRepaint) {
            qDebug() << "RegionPainter partial repaint"
                     << "logicalDirty=" << updateBounds
                     << "rectCount=" << updateRegion.rectCount()
                     << "backgroundDpr=" << dpr;
        }
#endif
#endif
        drawBackgroundTiles(painter, background, updateRegion);
    }

    if (!singleRegionFastPath && !selectionPreviewOwnedByOverlay) {
        // Draw dimmed overlay when the host cannot reuse a cached dimmed background.
        drawOverlay(painter);
    }

    // Draw detected window highlight (only during hover, before any selection)
    if (!m_selectionManager->hasActiveSelection()) {
        drawDetectedWindow(painter);
    }

    // Draw selection if active or complete
    if (m_selectionManager->hasActiveSelection() && selectionRect.isValid()) {
        if (!selectionPreviewOwnedByOverlay) {
            drawSelection(painter);
            drawDimensionInfo(painter);
        }

        // Draw annotations on top of selection (only when selection is established)
        if (!m_multiRegionMode && m_selectionManager->hasSelection()) {
            drawAnnotations(painter);
            drawCurrentAnnotation(painter);
        }
    }
}

void RegionPainter::drawDimmingOverlay(QPainter& painter, const QRect& clearRect, const QColor& dimColor)
{
    int w = m_parentWidget->width();
    int h = m_parentWidget->height();
    painter.fillRect(QRect(0, 0, w, clearRect.top()), dimColor);                                    // Top
    painter.fillRect(QRect(0, clearRect.bottom() + 1, w, h - clearRect.bottom() - 1), dimColor);    // Bottom
    painter.fillRect(QRect(0, clearRect.top(), clearRect.left(), clearRect.height()), dimColor);    // Left
    painter.fillRect(QRect(clearRect.right() + 1, clearRect.top(), w - clearRect.right() - 1, clearRect.height()), dimColor);  // Right
}

void RegionPainter::ensureDimmedBackgroundCache(const QPixmap& background) const
{
    const qreal dpr = background.devicePixelRatio();
    const qint64 cacheKey = background.cacheKey();
    if (!m_dimmedBackgroundCache.isNull() &&
        m_dimmedBackgroundCacheKey == cacheKey &&
        qFuzzyCompare(m_dimmedBackgroundCacheDpr, dpr)) {
        return;
    }

    QPixmap dimmed(background.size());
    dimmed.setDevicePixelRatio(dpr);
    dimmed.fill(Qt::transparent);

    QPainter dimPainter(&dimmed);
    dimPainter.drawPixmap(QPoint(0, 0), background);
    dimPainter.fillRect(
        QRectF(0, 0,
               static_cast<qreal>(background.width()) / dpr,
               static_cast<qreal>(background.height()) / dpr),
        kRegionDimColor);

    m_dimmedBackgroundCache = dimmed;
    m_dimmedBackgroundCacheKey = cacheKey;
    m_dimmedBackgroundCacheDpr = dpr;
}

void RegionPainter::drawBackgroundTiles(QPainter& painter,
                                        const QPixmap& background,
                                        const QRegion& updateRegion) const
{
    if (background.isNull() || updateRegion.isEmpty()) {
        return;
    }

    const qreal dpr = background.devicePixelRatio();
    for (QRegion::const_iterator it = updateRegion.begin(); it != updateRegion.end(); ++it) {
        const QRect updateRect = *it;
        if (!updateRect.isValid() || updateRect.isEmpty()) {
            continue;
        }
        const QRect sourceRect =
            CoordinateHelper::toPhysicalCoveringRect(updateRect, dpr).intersected(background.rect());
        if (!sourceRect.isValid() || sourceRect.isEmpty()) {
            continue;
        }
        const QRectF targetRect(
            static_cast<qreal>(sourceRect.x()) / dpr,
            static_cast<qreal>(sourceRect.y()) / dpr,
            static_cast<qreal>(sourceRect.width()) / dpr,
            static_cast<qreal>(sourceRect.height()) / dpr);
        painter.drawPixmap(targetRect, background, QRectF(sourceRect));
    }
}

void RegionPainter::drawOverlay(QPainter& painter)
{
    QRect sel = m_selectionManager->selectionRect();
    bool hasSelection = m_selectionManager->hasActiveSelection() && sel.isValid();

    if (m_multiRegionMode && m_multiRegionManager) {
        // Use QPainterPath subtraction instead of CompositionMode_Clear
        // CompositionMode_Clear doesn't work correctly on Windows (produces black instead of transparent)
        QPainterPath dimPath;
        dimPath.addRect(m_parentWidget->rect());

        const auto regions = m_multiRegionManager->regions();
        for (const auto& region : regions) {
            QPainterPath regionPath;
            regionPath.addRect(region.rect);
            dimPath = dimPath.subtracted(regionPath);
        }
        if (m_selectionManager->isSelecting() && m_multiRegionManager->activeIndex() < 0 && hasSelection) {
            QPainterPath selPath;
            selPath.addRect(sel);
            dimPath = dimPath.subtracted(selPath);
        }
        painter.fillPath(dimPath, kRegionDimColor);
        return;
    }

    if (hasSelection) {
        drawDimmingOverlay(painter, sel, kRegionDimColor);
    }
    else if (!m_highlightedWindowRect.isNull()) {
        drawDimmingOverlay(painter, m_highlightedWindowRect, kRegionDimColor);
    }
    else {
        painter.fillRect(m_parentWidget->rect(), kRegionDimColor);
    }
}

void RegionPainter::drawSelection(QPainter& painter)
{
    if (m_multiRegionMode && m_multiRegionManager) {
        drawMultiSelection(painter);
        return;
    }

    drawSelectionChrome(painter, m_selectionManager->selectionRect());
}

QRectF RegionPainter::alignedSelectionBorderRect(const QRect& selectionRect, qreal penWidth) const
{
    const qreal dpr = m_devicePixelRatio > 0.0 ? m_devicePixelRatio : 1.0;
    QRectF borderRect = QRectF(selectionRect.normalized()).adjusted(0.5, 0.5, -0.5, -0.5);
    const int physicalPenWidth = qMax(1, qRound(penWidth * dpr));
    if ((physicalPenWidth % 2) != 0) {
        const qreal offset = 0.5 / dpr;
        borderRect.translate(offset, offset);
    }
    return borderRect;
}

void RegionPainter::drawSelectionChrome(QPainter& painter, const QRect& selectionRect) const
{
    const QRect sel = selectionRect.normalized();
    if (!sel.isValid() || sel.isEmpty()) {
        return;
    }

    const int radius = effectiveCornerRadius(sel);

    QPen borderPen(QColor(0, 174, 255), kSelectionBorderWidth);
    borderPen.setJoinStyle(Qt::MiterJoin);
    borderPen.setCapStyle(Qt::SquareCap);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    const QRectF borderRect = alignedSelectionBorderRect(sel, borderPen.widthF());
    if (radius > 0) {
        painter.drawRoundedRect(borderRect, radius, radius);
    } else {
        painter.drawRect(borderRect);
    }

    painter.setBrush(QColor(0, 174, 255));
    painter.setPen(Qt::white);

    auto drawHandle = [&](int x, int y) {
        painter.drawEllipse(QPoint(x, y), kSelectionHandleRadius, kSelectionHandleRadius);
    };

    if (radius < 10) {
        drawHandle(sel.left(), sel.top());
        drawHandle(sel.right(), sel.top());
        drawHandle(sel.left(), sel.bottom());
        drawHandle(sel.right(), sel.bottom());
    }
    drawHandle(sel.center().x(), sel.top());
    drawHandle(sel.center().x(), sel.bottom());
    drawHandle(sel.left(), sel.center().y());
    drawHandle(sel.right(), sel.center().y());
}

void RegionPainter::drawDimensionInfo(QPainter& painter)
{
    if (m_multiRegionMode && m_multiRegionManager) {
        QRect activeInfoRect;
        const auto regions = m_multiRegionManager->regions();
        for (const auto& region : regions) {
            const QString dimensions = selectionSizeLabel(region.rect);
            QRect infoRect = drawDimensionInfoPanel(painter, region.rect, dimensions);
            if (region.isActive) {
                activeInfoRect = infoRect;
            }
        }

        if (activeInfoRect.isNull() && m_selectionManager->isSelecting()) {
            QRect sel = m_selectionManager->selectionRect();
            if (sel.isValid()) {
                const QString dimensions = selectionSizeLabel(sel);
                activeInfoRect = drawDimensionInfoPanel(painter, sel, dimensions);
            }
        }

        m_lastDimensionInfoRect = activeInfoRect;
        return;
    }

    QRect sel = m_selectionManager->selectionRect();
    const QString dimensions = selectionSizeLabel(sel);
    QRect textRect = drawDimensionInfoPanel(painter, sel, dimensions);
    m_lastDimensionInfoRect = textRect;
}

void RegionPainter::drawMultiSelection(QPainter& painter)
{
    if (!m_multiRegionManager) {
        return;
    }

    const auto regions = m_multiRegionManager->regions();
    for (const auto& region : regions) {
        QColor borderColor = region.color;
        int borderWidth = region.isActive ? 2 : 1;
        painter.setPen(QPen(borderColor, borderWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(region.rect);

        drawRegionBadge(painter, region.rect, borderColor, region.index, region.isActive);

        if (region.isActive) {
            const int handleSize = 8;
            painter.setBrush(borderColor);
            painter.setPen(Qt::white);
            auto drawHandle = [&](int x, int y) {
                painter.drawEllipse(QPoint(x, y), handleSize / 2, handleSize / 2);
            };
            drawHandle(region.rect.left(), region.rect.top());
            drawHandle(region.rect.right(), region.rect.top());
            drawHandle(region.rect.left(), region.rect.bottom());
            drawHandle(region.rect.right(), region.rect.bottom());
            drawHandle(region.rect.center().x(), region.rect.top());
            drawHandle(region.rect.center().x(), region.rect.bottom());
            drawHandle(region.rect.left(), region.rect.center().y());
            drawHandle(region.rect.right(), region.rect.center().y());
        }
    }

    if (m_selectionManager->isSelecting() && m_multiRegionManager->activeIndex() < 0) {
        QRect sel = m_selectionManager->selectionRect();
        if (sel.isValid()) {
            QColor previewColor = m_multiRegionManager->nextColor();
            painter.setPen(QPen(previewColor, 2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(sel);
            drawRegionBadge(painter, sel, previewColor, m_multiRegionManager->nextIndex(), true);
        }
    }

    if (m_replaceTargetIndex >= 0 && m_replaceTargetIndex < regions.size() &&
        m_replacePreviewRect.isValid() && !m_replacePreviewRect.isEmpty()) {
        const auto& targetRegion = regions[m_replaceTargetIndex];
        painter.setPen(QPen(targetRegion.color, 2, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(m_replacePreviewRect);
        drawRegionBadge(painter, m_replacePreviewRect, targetRegion.color, targetRegion.index, true);
    }
}

QRect RegionPainter::drawDimensionInfoPanel(QPainter& painter, const QRect& selectionRect,
                                            const QString& label) const
{
    QFont font = painter.font();
    font.setPointSize(12);
    painter.setFont(font);
    const QRect textRect = dimensionInfoPanelRect(selectionRect, label, font);

    auto styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    GlassRenderer::drawGlassPanel(painter, textRect, styleConfig, 6);

    painter.setPen(styleConfig.textColor);
    painter.drawText(textRect, Qt::AlignCenter, label);

    return textRect;
}

QRect RegionPainter::dimensionInfoPanelRect(const QRect& selectionRect, const QString& label,
                                            const QFont& baseFont) const
{
    const QRect sel = selectionRect.normalized();
    if (!m_parentWidget || !sel.isValid() || sel.isEmpty()) {
        return QRect();
    }

    QFont font(baseFont);
    font.setPointSize(12);
    QFontMetrics fm(font);

    const QString maxWidthText = tr("%1 x %2 px").arg(99999).arg(99999);
    const int fixedWidth = fm.horizontalAdvance(maxWidthText) + kDimensionPanelPadding;
    const int actualWidth = fm.horizontalAdvance(label) + kDimensionPanelPadding;
    const int width = qMax(fixedWidth, actualWidth);

    QRect textRect(0, 0, width, kDimensionPanelHeight);

    int textX = sel.left();
    int textY = sel.top() - textRect.height() - kDimensionPanelTopGap;
    if (textY < kDimensionPanelInset) {
        textY = sel.top() + kDimensionPanelInset;
        textX = sel.left() + kDimensionPanelInset;
    }

    textRect.moveTo(textX, textY);

    const int maxX = m_parentWidget->width() - kDimensionPanelInset;
    const int maxY = m_parentWidget->height() - kDimensionPanelInset;
    if (textRect.right() > maxX) {
        textRect.moveRight(maxX);
    }
    if (textRect.left() < kDimensionPanelInset) {
        textRect.moveLeft(kDimensionPanelInset);
    }
    if (textRect.bottom() > maxY) {
        textRect.moveBottom(maxY);
    }
    if (textRect.top() < kDimensionPanelInset) {
        textRect.moveTop(kDimensionPanelInset);
    }

    return textRect;
}

QRect RegionPainter::selectionChromeBounds(const QRect& selectionRect) const
{
    const QRect sel = selectionRect.normalized();
    if (!sel.isValid() || sel.isEmpty()) {
        return QRect();
    }

    return sel.adjusted(-kSelectionChromeMargin, -kSelectionChromeMargin,
                        kSelectionChromeMargin, kSelectionChromeMargin);
}

QRect RegionPainter::physicalSelectionRect(const QRect& selectionRect) const
{
    const qreal dpr = m_devicePixelRatio > 0.0 ? m_devicePixelRatio : 1.0;
    return CoordinateHelper::toPhysicalCoveringRect(selectionRect.normalized(), dpr);
}

QString RegionPainter::selectionSizeLabel(const QRect& selectionRect) const
{
    const QSize physicalSize = physicalSelectionRect(selectionRect).size();
    return tr("%1 x %2 px").arg(physicalSize.width()).arg(physicalSize.height());
}

void RegionPainter::drawRegionBadge(QPainter& painter, const QRect& selectionRect, const QColor& color,
                                    int index, bool isActive) const
{
    const int badgeSize = 18;
    QRect badgeRect(selectionRect.left() + 4, selectionRect.top() + 4, badgeSize, badgeSize);

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(badgeRect);

    if (isActive) {
        painter.setPen(QPen(Qt::white, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(badgeRect.adjusted(1, 1, -1, -1));
    }

    QFont font = painter.font();
    font.setPointSize(9);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(badgeRect, Qt::AlignCenter, QString::number(index));
}

void RegionPainter::drawDetectedWindow(QPainter& painter)
{
    if (m_highlightedWindowRect.isNull() || m_selectionManager->hasActiveSelection()) {
        return;
    }

    drawSelectionChrome(painter, m_highlightedWindowRect);
    const QString dimensions = selectionSizeLabel(m_highlightedWindowRect);
    drawDimensionInfoPanel(painter, m_highlightedWindowRect, dimensions);
}

void RegionPainter::drawAnnotations(QPainter& painter)
{
    if (!m_annotationLayer) {
        return;
    }

    if (m_parentWidget) {
        const QSize canvasSize = m_parentWidget->size();
        const qreal dpr = m_devicePixelRatio > 0.0 ? m_devicePixelRatio : 1.0;
        snaptray::annotation::SelectedAnnotationItems selectedItems;
        selectedItems.text = getSelectedTextAnnotation();
        selectedItems.emoji = getSelectedEmojiStickerAnnotation();
        selectedItems.shape = getSelectedShapeAnnotation();
        selectedItems.arrow = getSelectedArrowAnnotation();
        selectedItems.polyline = getSelectedPolylineAnnotation();
        snaptray::annotation::drawAnnotationVisuals(
            painter,
            m_annotationLayer,
            canvasSize,
            dpr,
            QPoint(0, 0),
            m_annotationLayer->selectedIndex() >= 0,
            selectedItems);
    } else {
        qWarning() << "RegionPainter::drawAnnotations: m_parentWidget is null, "
                      "falling back to uncached draw";
        m_annotationLayer->draw(painter);
    }
}

void RegionPainter::drawCurrentAnnotation(QPainter& painter)
{
    if (!m_toolManager) {
        return;
    }

    // Use ToolManager for tools it handles.
    ToolId tool = static_cast<ToolId>(m_currentTool);
    if (ToolTraits::isToolManagerHandledTool(tool)) {
        m_toolManager->drawCurrentPreview(painter);
    }
}

QRect RegionPainter::getWindowHighlightVisualRect(const QRect& windowRect) const
{
    if (!m_parentWidget || windowRect.isNull()) {
        return QRect();
    }

    const QString dimensions = selectionSizeLabel(windowRect);
    const QFont baseFont;
    const QRect panelRect = dimensionInfoPanelRect(windowRect, dimensions, baseFont);
    return selectionChromeBounds(windowRect).united(panelRect);
}

int RegionPainter::effectiveCornerRadius(const QRect& selectionRect) const
{
    QRect sel = selectionRect.normalized();
    if (sel.isEmpty()) return 0;
    // Cap radius at half the smaller dimension
    int maxRadius = qMin(sel.width(), sel.height()) / 2;
    return qMin(m_cornerRadius, maxRadius);
}

TextBoxAnnotation* RegionPainter::getSelectedTextAnnotation() const
{
    if (!m_annotationLayer) {
        return nullptr;
    }
    if (m_annotationLayer->selectedIndex() >= 0) {
        return dynamic_cast<TextBoxAnnotation*>(m_annotationLayer->selectedItem());
    }
    return nullptr;
}

EmojiStickerAnnotation* RegionPainter::getSelectedEmojiStickerAnnotation() const
{
    if (!m_annotationLayer) {
        return nullptr;
    }
    if (m_annotationLayer->selectedIndex() >= 0) {
        return dynamic_cast<EmojiStickerAnnotation*>(m_annotationLayer->selectedItem());
    }
    return nullptr;
}

ShapeAnnotation* RegionPainter::getSelectedShapeAnnotation() const
{
    if (!m_annotationLayer) {
        return nullptr;
    }
    if (m_annotationLayer->selectedIndex() >= 0) {
        return dynamic_cast<ShapeAnnotation*>(m_annotationLayer->selectedItem());
    }
    return nullptr;
}

ArrowAnnotation* RegionPainter::getSelectedArrowAnnotation() const
{
    if (!m_annotationLayer) {
        return nullptr;
    }
    if (m_annotationLayer->selectedIndex() >= 0) {
        return dynamic_cast<ArrowAnnotation*>(m_annotationLayer->selectedItem());
    }
    return nullptr;
}

PolylineAnnotation* RegionPainter::getSelectedPolylineAnnotation() const
{
    if (!m_annotationLayer) {
        return nullptr;
    }
    if (m_annotationLayer->selectedIndex() >= 0) {
        return dynamic_cast<PolylineAnnotation*>(m_annotationLayer->selectedItem());
    }
    return nullptr;
}
