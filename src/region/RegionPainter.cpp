#include "region/RegionPainter.h"
#include "region/SelectionStateManager.h"
#include "region/RegionControlWidget.h"
#include "region/MultiRegionManager.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/TextBoxAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "tools/ToolManager.h"
#include "toolbar/ToolbarCore.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include "TransformationGizmo.h"
#include "RegionSelector.h"  // For isToolManagerHandledTool
#include "utils/CoordinateHelper.h"

#include <QPainter>
#include <QPainterPath>
#include <QWidget>
#include <QtMath>

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

void RegionPainter::setToolbar(ToolbarCore* toolbar)
{
    m_toolbar = toolbar;
}

void RegionPainter::setRegionControlWidget(RegionControlWidget* widget)
{
    m_regionControlWidget = widget;
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

void RegionPainter::setDetectedWindowTitle(const QString& title)
{
    m_detectedWindowTitle = title;
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

void RegionPainter::invalidateOverlayCache()
{
    m_overlayCacheValid = false;
}

void RegionPainter::paint(QPainter& painter, const QPixmap& background, const QRect& dirtyRect)
{
    if (!m_parentWidget || !m_selectionManager) {
        return;
    }

    // Use dirty rect for optimized partial repainting
    // Only draw the background portion that needs updating
    const QRect updateRect = dirtyRect.isEmpty() ? m_parentWidget->rect() : dirtyRect;

    // Draw only the dirty portion of the background
    // This is much faster than drawing the entire background on high-DPI screens
    if (!background.isNull()) {
        // Calculate source rect in pixmap coordinates (accounting for device pixel ratio)
        const qreal dpr = background.devicePixelRatio();
        const QRect sourceRect = CoordinateHelper::toPhysical(updateRect, dpr);
        painter.drawPixmap(updateRect, background, sourceRect);
    }

    // Draw dimmed overlay
    drawOverlay(painter);

    // Draw detected window highlight (only during hover, before any selection)
    if (!m_selectionManager->hasActiveSelection()) {
        drawDetectedWindow(painter);
    }

    // Draw selection if active or complete
    QRect selectionRect = m_selectionManager->selectionRect();
    if (m_selectionManager->hasActiveSelection() && selectionRect.isValid()) {
        drawSelection(painter);
        drawDimensionInfo(painter);

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

void RegionPainter::drawOverlay(QPainter& painter)
{
    QColor dimColor(0, 0, 0, 100);

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
        painter.fillPath(dimPath, dimColor);
        return;
    }

    if (hasSelection) {
        drawDimmingOverlay(painter, sel, dimColor);
    }
    else if (!m_highlightedWindowRect.isNull()) {
        drawDimmingOverlay(painter, m_highlightedWindowRect, dimColor);
    }
    else {
        painter.fillRect(m_parentWidget->rect(), dimColor);
    }
}

void RegionPainter::drawSelection(QPainter& painter)
{
    if (m_multiRegionMode && m_multiRegionManager) {
        drawMultiSelection(painter);
        return;
    }

    QRect sel = m_selectionManager->selectionRect();
    int radius = effectiveCornerRadius();

    // Draw selection border
    painter.setPen(QPen(QColor(0, 174, 255), 2));
    painter.setBrush(Qt::NoBrush);
    if (radius > 0) {
        painter.drawRoundedRect(sel, radius, radius);
    } else {
        painter.drawRect(sel);
    }

    // Draw corner and edge handles
    const int handleSize = 8;
    QColor handleColor(0, 174, 255);
    painter.setBrush(handleColor);
    painter.setPen(Qt::white);

    // Corner handles (circles for Snipaste style)
    // Skip corner handles when corner radius is large (they would overlap the rounded corner)
    auto drawHandle = [&](int x, int y) {
        painter.drawEllipse(QPoint(x, y), handleSize / 2, handleSize / 2);
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
            QString dimensions = QString("%1 x %2  pt").arg(region.rect.width()).arg(region.rect.height());
            QRect infoRect = drawDimensionInfoPanel(painter, region.rect, dimensions);
            if (region.isActive) {
                activeInfoRect = infoRect;
            }
        }

        if (activeInfoRect.isNull() && m_selectionManager->isSelecting()) {
            QRect sel = m_selectionManager->selectionRect();
            if (sel.isValid()) {
                QString dimensions = QString("%1 x %2  pt").arg(sel.width()).arg(sel.height());
                activeInfoRect = drawDimensionInfoPanel(painter, sel, dimensions);
            }
        }

        if (!activeInfoRect.isNull()) {
            drawRegionControlWidget(painter, activeInfoRect);
        }
        return;
    }

    QRect sel = m_selectionManager->selectionRect();
    QString dimensions = QString("%1 x %2  pt").arg(sel.width()).arg(sel.height());
    QRect textRect = drawDimensionInfoPanel(painter, sel, dimensions);

    // Draw region control widget next to dimension info
    drawRegionControlWidget(painter, textRect);
}

void RegionPainter::drawRegionControlWidget(QPainter& painter, const QRect& dimensionInfoRect)
{
    if (!m_regionControlWidget) {
        return;
    }

    // Show region control widget when selection has been made
    if (m_selectionManager->hasSelection()) {
        m_regionControlWidget->setVisible(true);
        m_regionControlWidget->updatePosition(dimensionInfoRect, m_parentWidget->width(), m_parentWidget->height());
        m_regionControlWidget->draw(painter);
    } else {
        m_regionControlWidget->setVisible(false);
    }
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
}

QRect RegionPainter::drawDimensionInfoPanel(QPainter& painter, const QRect& selectionRect,
                                            const QString& label) const
{
    QFont font = painter.font();
    font.setPointSize(12);
    painter.setFont(font);

    QFontMetrics fm(font);

    // Calculate fixed minimum width for "9999 x 9999  pt" to prevent jittering
    QString maxWidthText = "9999 x 9999  pt";
    int fixedWidth = fm.horizontalAdvance(maxWidthText) + 24;  // +24 for padding

    int actualWidth = fm.horizontalAdvance(label) + 24;
    int width = qMax(fixedWidth, actualWidth);

    // Use fixed height 28px to match RegionControlWidget
    static constexpr int PANEL_HEIGHT = 28;
    QRect textRect(0, 0, width, PANEL_HEIGHT);

    int textX = selectionRect.left();
    int textY = selectionRect.top() - textRect.height() - 8;
    if (textY < 5) {
        textY = selectionRect.top() + 5;
        textX = selectionRect.left() + 5;
    }

    textRect.moveTo(textX, textY);

    auto styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    GlassRenderer::drawGlassPanel(painter, textRect, styleConfig, 6);

    painter.setPen(styleConfig.textColor);
    painter.drawText(textRect, Qt::AlignCenter, label);

    return textRect;
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

    // Dashed border to distinguish from final selection
    QPen dashedPen(QColor(0, 174, 255), 2, Qt::DashLine);
    painter.setPen(dashedPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(m_highlightedWindowRect);

    // Show window dimensions hint
    if (!m_detectedWindowTitle.isEmpty()) {
        drawWindowHint(painter, m_detectedWindowTitle);
    }
}

void RegionPainter::drawWindowHint(QPainter& painter, const QString& title)
{
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QFontMetrics fm(font);
    QString displayTitle = fm.elidedText(title, Qt::ElideRight, 200);
    QRect textRect = fm.boundingRect(displayTitle);
    textRect.adjust(-8, -4, 8, 4);

    int w = m_parentWidget->width();
    int h = m_parentWidget->height();

    // For small elements (like menu bar icons), position to the right
    // For larger windows, position below
    bool isSmallElement = m_highlightedWindowRect.width() < 60 || m_highlightedWindowRect.height() < 60;

    int hintX, hintY;

    if (isSmallElement) {
        // Position to the right of the element
        hintX = m_highlightedWindowRect.right() + 4;
        hintY = m_highlightedWindowRect.top();

        // If no space on right, try left
        if (hintX + textRect.width() > w - 5) {
            hintX = m_highlightedWindowRect.left() - textRect.width() - 4;
        }
        // If no space on left either, position below
        if (hintX < 5) {
            hintX = m_highlightedWindowRect.left();
            hintY = m_highlightedWindowRect.bottom() + 4;
        }
    }
    else {
        // Position below the detected window
        hintX = m_highlightedWindowRect.left();
        hintY = m_highlightedWindowRect.bottom() + 4;

        // If no space below, position above
        if (hintY + textRect.height() > h - 5) {
            hintY = m_highlightedWindowRect.top() - textRect.height() - 4;
        }
    }

    textRect.moveTo(hintX, hintY);

    // Keep on screen
    if (textRect.right() > w - 5) {
        textRect.moveRight(w - 5);
    }
    if (textRect.left() < 5) {
        textRect.moveLeft(5);
    }
    if (textRect.bottom() > h - 5) {
        textRect.moveBottom(h - 5);
    }
    if (textRect.top() < 5) {
        textRect.moveTop(5);
    }

    // Draw background pill
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRoundedRect(textRect, 4, 4);

    // Draw text
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, displayTitle);
}

void RegionPainter::drawAnnotations(QPainter& painter)
{
    if (!m_annotationLayer) {
        return;
    }

    m_annotationLayer->draw(painter);

    // Draw transformation gizmo for selected text annotation
    if (auto* textItem = getSelectedTextAnnotation()) {
        TransformationGizmo::draw(painter, textItem);
    }

    // Draw transformation gizmo for selected emoji sticker annotation
    if (auto* emojiItem = getSelectedEmojiStickerAnnotation()) {
        TransformationGizmo::draw(painter, emojiItem);
    }

    // Draw transformation gizmo for selected arrow annotation
    if (auto* arrowItem = getSelectedArrowAnnotation()) {
        TransformationGizmo::draw(painter, arrowItem);
    }

    // Draw transformation gizmo for selected polyline annotation
    if (auto* polylineItem = getSelectedPolylineAnnotation()) {
        TransformationGizmo::draw(painter, polylineItem);
    }
}

void RegionPainter::drawCurrentAnnotation(QPainter& painter)
{
    if (!m_toolManager) {
        return;
    }

    // Use ToolManager for tools it handles
    ToolId tool = static_cast<ToolId>(m_currentTool);
    if (isToolManagerHandledTool(tool)) {
        m_toolManager->drawCurrentPreview(painter);
        return;
    }

    // Text tool is handled by InlineTextEditor, no preview needed here
}

QRect RegionPainter::getWindowHighlightVisualRect(const QRect& windowRect, const QString& title) const
{
    if (!m_parentWidget || windowRect.isNull()) {
        return QRect();
    }

    // Start with the window rect itself (this covers the highlight border)
    // Add a small margin for the border (2px width + potential antialiasing)
    QRect visualRect = windowRect.adjusted(-2, -2, 2, 2);

    if (title.isEmpty()) {
        return visualRect;
    }

    // Logic duplicating drawWindowHint to determine hint position
    QFont font; 
    font.setPointSize(11);
    
    QFontMetrics fm(font);
    QString displayTitle = fm.elidedText(title, Qt::ElideRight, 200);
    QRect textRect = fm.boundingRect(displayTitle);
    textRect.adjust(-8, -4, 8, 4);

    int w = m_parentWidget->width();
    int h = m_parentWidget->height();

    bool isSmallElement = windowRect.width() < 60 || windowRect.height() < 60;

    int hintX, hintY;

    if (isSmallElement) {
        hintX = windowRect.right() + 4;
        hintY = windowRect.top();

        if (hintX + textRect.width() > w - 5) {
            hintX = windowRect.left() - textRect.width() - 4;
        }
        if (hintX < 5) {
            hintX = windowRect.left();
            hintY = windowRect.bottom() + 4;
        }
    }
    else {
        hintX = windowRect.left();
        hintY = windowRect.bottom() + 4;

        if (hintY + textRect.height() > h - 5) {
            hintY = windowRect.top() - textRect.height() - 4;
        }
    }

    textRect.moveTo(hintX, hintY);

    if (textRect.right() > w - 5) {
        textRect.moveRight(w - 5);
    }
    if (textRect.left() < 5) {
        textRect.moveLeft(5);
    }
    if (textRect.bottom() > h - 5) {
        textRect.moveBottom(h - 5);
    }
    if (textRect.top() < 5) {
        textRect.moveTop(5);
    }

    return visualRect.united(textRect);
}

int RegionPainter::effectiveCornerRadius() const
{
    QRect sel = m_selectionManager->selectionRect();
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
