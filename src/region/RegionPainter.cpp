#include "region/RegionPainter.h"
#include "region/SelectionStateManager.h"
#include "region/RadiusSliderWidget.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/TextAnnotation.h"
#include "tools/ToolManager.h"
#include "ToolbarWidget.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include "TransformationGizmo.h"
#include "RegionSelector.h"  // For ToolbarButton enum and isToolManagerHandledTool

#include <QPainter>
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

void RegionPainter::setToolbar(ToolbarWidget* toolbar)
{
    m_toolbar = toolbar;
}

void RegionPainter::setRadiusSliderWidget(RadiusSliderWidget* widget)
{
    m_radiusSliderWidget = widget;
}

void RegionPainter::setParentWidget(QWidget* widget)
{
    m_parentWidget = widget;
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

void RegionPainter::paint(QPainter& painter, const QPixmap& background)
{
    if (!m_parentWidget || !m_selectionManager) {
        return;
    }

    // Draw the captured background scaled to fit the widget (logical pixels)
    painter.drawPixmap(m_parentWidget->rect(), background);

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
        if (m_selectionManager->hasSelection()) {
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
    QRect sel = m_selectionManager->selectionRect();

    // Show dimensions with "pt" suffix like Snipaste
    QString dimensions = QString("%1 x %2  pt").arg(sel.width()).arg(sel.height());

    QFont font = painter.font();
    font.setPointSize(12);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(dimensions);
    textRect.adjust(-12, -6, 12, 6);

    // Position above selection
    int textX = sel.left();
    int textY = sel.top() - textRect.height() - 8;
    if (textY < 5) {
        textY = sel.top() + 5;
        textX = sel.left() + 5;
    }

    textRect.moveTo(textX, textY);

    // Draw glass panel background (matching radius slider style)
    auto styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    GlassRenderer::drawGlassPanel(painter, textRect, styleConfig, 6);

    // Draw text
    painter.setPen(styleConfig.textColor);
    painter.drawText(textRect, Qt::AlignCenter, dimensions);

    // Draw radius slider next to dimension info
    drawRadiusSlider(painter, textRect);
}

void RegionPainter::drawRadiusSlider(QPainter& painter, const QRect& dimensionInfoRect)
{
    if (!m_radiusSliderWidget) {
        return;
    }

    // Show radius slider when selection is complete
    if (m_selectionManager->isComplete()) {
        m_radiusSliderWidget->setVisible(true);
        m_radiusSliderWidget->updatePosition(dimensionInfoRect, m_parentWidget->width());
        m_radiusSliderWidget->draw(painter);
    } else {
        m_radiusSliderWidget->setVisible(false);
    }
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
}

void RegionPainter::drawCurrentAnnotation(QPainter& painter)
{
    if (!m_toolManager) {
        return;
    }

    // Use ToolManager for tools it handles
    ToolbarButton tool = static_cast<ToolbarButton>(m_currentTool);
    if (isToolManagerHandledTool(tool)) {
        m_toolManager->drawCurrentPreview(painter);
        return;
    }

    // Text tool is handled by InlineTextEditor, no preview needed here
}

int RegionPainter::effectiveCornerRadius() const
{
    QRect sel = m_selectionManager->selectionRect();
    if (sel.isEmpty()) return 0;
    // Cap radius at half the smaller dimension
    int maxRadius = qMin(sel.width(), sel.height()) / 2;
    return qMin(m_cornerRadius, maxRadius);
}

TextAnnotation* RegionPainter::getSelectedTextAnnotation() const
{
    if (!m_annotationLayer) {
        return nullptr;
    }
    if (m_annotationLayer->selectedIndex() >= 0) {
        return dynamic_cast<TextAnnotation*>(m_annotationLayer->selectedItem());
    }
    return nullptr;
}
