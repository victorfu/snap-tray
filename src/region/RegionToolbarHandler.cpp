#include "region/RegionToolbarHandler.h"
#include "region/SelectionStateManager.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/StepBadgeAnnotation.h"
#include "tools/ToolManager.h"
#include "tools/ToolId.h"
#include "tools/handlers/EraserToolHandler.h"
#include "ToolbarWidget.h"
#include "ColorAndWidthWidget.h"
#include "IconRenderer.h"
#include "OCRManager.h"
#include "PlatformFeatures.h"
#include "RegionSelector.h"  // For ToolbarButton enum

#include <QWidget>
#include <QDebug>

RegionToolbarHandler::RegionToolbarHandler(QObject* parent)
    : QObject(parent)
    , m_currentTool(ToolbarButton::Selection)
    , m_stepBadgeSize(StepBadgeSize::Medium)
{
}

void RegionToolbarHandler::setToolbar(ToolbarWidget* toolbar)
{
    m_toolbar = toolbar;
}

void RegionToolbarHandler::setToolManager(ToolManager* manager)
{
    m_toolManager = manager;
}

void RegionToolbarHandler::setAnnotationLayer(AnnotationLayer* layer)
{
    m_annotationLayer = layer;
}

void RegionToolbarHandler::setColorAndWidthWidget(ColorAndWidthWidget* widget)
{
    m_colorAndWidthWidget = widget;
}

void RegionToolbarHandler::setSelectionManager(SelectionStateManager* manager)
{
    m_selectionManager = manager;
}

void RegionToolbarHandler::setOCRManager(OCRManager* manager)
{
    m_ocrManager = manager;
}

void RegionToolbarHandler::setParentWidget(QWidget* widget)
{
    m_parentWidget = widget;
}

void RegionToolbarHandler::setStepBadgeSize(StepBadgeSize size)
{
    m_stepBadgeSize = size;
}

void RegionToolbarHandler::setupToolbarButtons()
{
    if (!m_toolbar) return;

    // Load icons
    IconRenderer& iconRenderer = IconRenderer::instance();
    iconRenderer.loadIcon("selection", ":/icons/icons/selection.svg");
    iconRenderer.loadIcon("arrow", ":/icons/icons/arrow.svg");
    iconRenderer.loadIcon("polyline", ":/icons/icons/polyline.svg");
    iconRenderer.loadIcon("pencil", ":/icons/icons/pencil.svg");
    iconRenderer.loadIcon("marker", ":/icons/icons/marker.svg");
    iconRenderer.loadIcon("rectangle", ":/icons/icons/rectangle.svg");
    iconRenderer.loadIcon("ellipse", ":/icons/icons/ellipse.svg");
    iconRenderer.loadIcon("shape", ":/icons/icons/shape.svg");
    iconRenderer.loadIcon("text", ":/icons/icons/text.svg");
    iconRenderer.loadIcon("mosaic", ":/icons/icons/mosaic.svg");
    iconRenderer.loadIcon("step-badge", ":/icons/icons/step-badge.svg");
    iconRenderer.loadIcon("eraser", ":/icons/icons/eraser.svg");
    iconRenderer.loadIcon("undo", ":/icons/icons/undo.svg");
    iconRenderer.loadIcon("redo", ":/icons/icons/redo.svg");
    iconRenderer.loadIcon("cancel", ":/icons/icons/cancel.svg");
    if (PlatformFeatures::instance().isOCRAvailable()) {
        iconRenderer.loadIcon("ocr", ":/icons/icons/ocr.svg");
    }
    iconRenderer.loadIcon("auto-blur", ":/icons/icons/auto-blur.svg");
    iconRenderer.loadIcon("pin", ":/icons/icons/pin.svg");
    iconRenderer.loadIcon("record", ":/icons/icons/record.svg");
    iconRenderer.loadIcon("scroll-capture", ":/icons/icons/scroll-capture.svg");
    iconRenderer.loadIcon("save", ":/icons/icons/save.svg");
    iconRenderer.loadIcon("copy", ":/icons/icons/copy.svg");
    iconRenderer.loadIcon("multi-region", ":/icons/icons/multi-region.svg");
    iconRenderer.loadIcon("done", ":/icons/icons/done.svg");
    // Shape and arrow style icons for ColorAndWidthWidget sections
    iconRenderer.loadIcon("shape-filled", ":/icons/icons/shape-filled.svg");
    iconRenderer.loadIcon("shape-outline", ":/icons/icons/shape-outline.svg");
    iconRenderer.loadIcon("arrow-none", ":/icons/icons/arrow-none.svg");
    iconRenderer.loadIcon("arrow-end", ":/icons/icons/arrow-end.svg");
    iconRenderer.loadIcon("arrow-end-outline", ":/icons/icons/arrow-end-outline.svg");
    iconRenderer.loadIcon("arrow-end-line", ":/icons/icons/arrow-end-line.svg");
    iconRenderer.loadIcon("arrow-both", ":/icons/icons/arrow-both.svg");
    iconRenderer.loadIcon("arrow-both-outline", ":/icons/icons/arrow-both-outline.svg");

    // Configure buttons
    QVector<ToolbarWidget::ButtonConfig> buttons;
    if (m_multiRegionMode) {
        buttons.append({ static_cast<int>(ToolbarButton::MultiRegionDone), "done", "Done", false });
        buttons.append({ static_cast<int>(ToolbarButton::Cancel), "cancel", "Cancel (Esc)", true });
        m_toolbar->setButtons(buttons);
        m_toolbar->setActiveButtonIds({});
        m_toolbar->setIconColorProvider([this](int buttonId, bool isActive, bool isHovered) {
            return getToolbarIconColor(buttonId, isActive, isHovered);
        });
        return;
    }

    buttons.append({ static_cast<int>(ToolbarButton::Selection), "selection", "Selection", false });
    buttons.append({ static_cast<int>(ToolbarButton::Shape), "shape", "Shape", false });
    buttons.append({ static_cast<int>(ToolbarButton::Arrow), "arrow", "Arrow", false });
    buttons.append({ static_cast<int>(ToolbarButton::Pencil), "pencil", "Pencil", false });
    buttons.append({ static_cast<int>(ToolbarButton::Marker), "marker", "Marker", false });
    buttons.append({ static_cast<int>(ToolbarButton::Text), "text", "Text", false });
    buttons.append({ static_cast<int>(ToolbarButton::Mosaic), "mosaic", "Mosaic", false });
    buttons.append({ static_cast<int>(ToolbarButton::StepBadge), "step-badge", "Step Badge", false });
    buttons.append({ static_cast<int>(ToolbarButton::Eraser), "eraser", "Eraser", false });
    buttons.append({ static_cast<int>(ToolbarButton::Undo), "undo", "Undo", true });
    buttons.append({ static_cast<int>(ToolbarButton::Redo), "redo", "Redo", false });
    buttons.append({ static_cast<int>(ToolbarButton::Cancel), "cancel", "Cancel (Esc)", true });  // separator before
    if (PlatformFeatures::instance().isOCRAvailable()) {
        buttons.append({ static_cast<int>(ToolbarButton::OCR), "ocr", "OCR Text Recognition", false });
    }
    buttons.append({ static_cast<int>(ToolbarButton::Record), "record", "Screen Recording (R)", false });
    buttons.append({ static_cast<int>(ToolbarButton::MultiRegion), "multi-region", "Multi-Region Capture (M)", false });
#ifdef SNAPTRAY_ENABLE_DEV_FEATURES
    buttons.append({ static_cast<int>(ToolbarButton::ScrollCapture), "scroll-capture", "Scrolling Capture (S)", false });
#endif
    buttons.append({ static_cast<int>(ToolbarButton::Pin), "pin", "Pin to Screen (Enter)", false });
    buttons.append({ static_cast<int>(ToolbarButton::Save), "save", "Save (Ctrl+S)", false });
    buttons.append({ static_cast<int>(ToolbarButton::Copy), "copy", "Copy (Ctrl+C)", false });

    m_toolbar->setButtons(buttons);

    // Set which buttons are "active" type (annotation tools that stay highlighted)
    QVector<int> activeButtonIds = {
        static_cast<int>(ToolbarButton::Selection),
        static_cast<int>(ToolbarButton::Arrow),
        static_cast<int>(ToolbarButton::Pencil),
        static_cast<int>(ToolbarButton::Marker),
        static_cast<int>(ToolbarButton::Shape),
        static_cast<int>(ToolbarButton::Text),
        static_cast<int>(ToolbarButton::Mosaic),
        static_cast<int>(ToolbarButton::StepBadge),
        static_cast<int>(ToolbarButton::Eraser)
    };
    m_toolbar->setActiveButtonIds(activeButtonIds);

    // Set icon color provider
    m_toolbar->setIconColorProvider([this](int buttonId, bool isActive, bool isHovered) {
        return getToolbarIconColor(buttonId, isActive, isHovered);
    });
}

QColor RegionToolbarHandler::getToolbarIconColor(int buttonId, bool isActive, bool isHovered) const
{
    Q_UNUSED(isHovered);

    if (!m_toolbar) return QColor(128, 128, 128);

    const auto& style = m_toolbar->styleConfig();
    ToolbarButton btn = static_cast<ToolbarButton>(buttonId);

    // Show gray for unavailable features
    if (btn == ToolbarButton::OCR && !m_ocrManager) {
        return QColor(128, 128, 128);
    }

    // Show yellow when processing
    if (btn == ToolbarButton::OCR && m_ocrInProgress) {
        return QColor(255, 200, 100);
    }

    if (btn == ToolbarButton::MultiRegionDone) {
        return style.iconActionColor;
    }

    // Show gray for Undo when nothing to undo
    if (btn == ToolbarButton::Undo && m_annotationLayer && !m_annotationLayer->canUndo()) {
        return QColor(128, 128, 128);
    }

    // Show gray for Redo when nothing to redo
    if (btn == ToolbarButton::Redo && m_annotationLayer && !m_annotationLayer->canRedo()) {
        return QColor(128, 128, 128);
    }

    // All icons use the same color scheme as Pencil
    if (isActive) {
        return style.iconActiveColor;
    }
    return style.iconNormalColor;
}

void RegionToolbarHandler::handleToolbarClick(ToolbarButton button)
{
    // Save eraser width and clear hover when switching FROM Eraser to another tool
    if (m_currentTool == ToolbarButton::Eraser && button != ToolbarButton::Eraser) {
        saveEraserWidthAndClearHover();
    }

    // Restore standard width when switching to annotation tools (except Eraser)
    if (button != ToolbarButton::Eraser && isAnnotationTool(button)) {
        restoreStandardWidth();
    }

    switch (button) {
    case ToolbarButton::Selection:
        // Selection has no sub-toolbar, just switch tool
        m_currentTool = button;
        m_showSubToolbar = true;
        emit showSizeSectionRequested(false);
        qDebug() << "Tool selected:" << static_cast<int>(button);
        emit toolChanged(m_currentTool, m_showSubToolbar);
        emit updateRequested();
        break;

    case ToolbarButton::Arrow:
    case ToolbarButton::Pencil:
    case ToolbarButton::Marker:
    case ToolbarButton::Shape:
    case ToolbarButton::Text:
        handleAnnotationTool(button);
        break;

    case ToolbarButton::StepBadge:
        handleStepBadgeTool();
        break;

    case ToolbarButton::Mosaic:
        handleMosaicTool();
        break;

    case ToolbarButton::Eraser:
        handleEraserTool();
        break;

    case ToolbarButton::Undo:
    case ToolbarButton::Redo:
    case ToolbarButton::Cancel:
    case ToolbarButton::OCR:
    case ToolbarButton::Pin:
    case ToolbarButton::Record:
    case ToolbarButton::ScrollCapture:
    case ToolbarButton::Save:
    case ToolbarButton::Copy:
        handleActionButton(button);
        break;

    case ToolbarButton::MultiRegion:
        emit multiRegionToggled(!m_multiRegionMode);
        break;

    case ToolbarButton::MultiRegionDone:
        emit multiRegionDoneRequested();
        break;

    default:
        break;
    }
}

void RegionToolbarHandler::handleAnnotationTool(ToolbarButton button)
{
    if (m_currentTool == button) {
        // Same tool clicked - toggle sub-toolbar visibility
        m_showSubToolbar = !m_showSubToolbar;
    } else {
        // Different tool - select it and show sub-toolbar
        m_currentTool = button;
        m_showSubToolbar = true;
    }
    emit showSizeSectionRequested(false);
    qDebug() << "Tool selected:" << static_cast<int>(button) << "showSubToolbar:" << m_showSubToolbar;
    emit toolChanged(m_currentTool, m_showSubToolbar);
    emit updateRequested();
}

void RegionToolbarHandler::handleStepBadgeTool()
{
    if (m_currentTool == ToolbarButton::StepBadge) {
        // Same tool clicked - toggle sub-toolbar visibility
        m_showSubToolbar = !m_showSubToolbar;
    } else {
        // Different tool - select it and show sub-toolbar
        m_currentTool = ToolbarButton::StepBadge;
        if (m_toolManager) {
            m_toolManager->setCurrentTool(ToolId::StepBadge);
        }
        m_showSubToolbar = true;
    }
    // Show size section, hide width section for step badge
    emit showSizeSectionRequested(true);
    emit showWidthSectionRequested(false);
    emit stepBadgeSizeRequested(m_stepBadgeSize);
    // Set width to radius for tool context
    if (m_toolManager) {
        m_toolManager->setWidth(StepBadgeAnnotation::radiusForSize(m_stepBadgeSize));
    }
    qDebug() << "StepBadge tool selected, showSubToolbar:" << m_showSubToolbar;
    emit toolChanged(m_currentTool, m_showSubToolbar);
    emit updateRequested();
}

void RegionToolbarHandler::handleMosaicTool()
{
    if (m_currentTool == ToolbarButton::Mosaic) {
        // Same tool clicked - toggle sub-toolbar visibility
        m_showSubToolbar = !m_showSubToolbar;
    } else {
        // Different tool - select it and show sub-toolbar
        m_currentTool = ToolbarButton::Mosaic;
        if (m_toolManager) {
            m_toolManager->setCurrentTool(ToolId::Mosaic);
        }
        m_showSubToolbar = true;
    }
    // Use shared WidthSection for Mosaic (synced with other tools)
    emit showWidthSectionRequested(true);
    emit widthSectionHiddenRequested(false);
    emit showMosaicBlurTypeSectionRequested(true);  // Show blur type selector
    emit mosaicBlurTypeRequested(m_mosaicBlurType);  // Set current blur type
    emit currentWidthRequested(m_annotationWidth);
    if (m_toolManager) {
        m_toolManager->setWidth(m_annotationWidth);
    }
    emit showSizeSectionRequested(false);
    // Mosaic tool doesn't use color, hide color section
    emit showColorSectionRequested(false);
    qDebug() << "Mosaic tool selected, showSubToolbar:" << m_showSubToolbar;
    emit toolChanged(m_currentTool, m_showSubToolbar);
    emit updateRequested();
}

void RegionToolbarHandler::handleEraserTool()
{
    // Eraser has no sub-toolbar, just switch tool
    m_currentTool = ToolbarButton::Eraser;
    m_showSubToolbar = true;
    if (m_toolManager) {
        m_toolManager->setCurrentTool(ToolId::Eraser);
        m_toolManager->setWidth(m_eraserWidth);
    }
    qDebug() << "Eraser tool selected - drag over annotations to erase them";
    emit toolChanged(m_currentTool, m_showSubToolbar);
    emit updateRequested();
}

void RegionToolbarHandler::handleActionButton(ToolbarButton button)
{
    switch (button) {
    case ToolbarButton::Undo:
        if (m_annotationLayer && m_annotationLayer->canUndo()) {
            emit undoRequested();
            qDebug() << "Undo";
            emit updateRequested();
        }
        break;

    case ToolbarButton::Redo:
        if (m_annotationLayer && m_annotationLayer->canRedo()) {
            emit redoRequested();
            qDebug() << "Redo";
            emit updateRequested();
        }
        break;

    case ToolbarButton::Cancel:
        if (m_multiRegionMode) {
            emit multiRegionCancelRequested();
        } else {
            emit cancelRequested();
        }
        break;

    case ToolbarButton::OCR:
        emit ocrRequested();
        break;

    case ToolbarButton::Pin:
        emit pinRequested();
        break;

    case ToolbarButton::Record:
        emit recordRequested();
        break;

    case ToolbarButton::ScrollCapture:
        emit scrollCaptureRequested();
        break;

    case ToolbarButton::Save:
        emit saveRequested();
        break;

    case ToolbarButton::Copy:
        emit copyRequested();
        break;

    default:
        break;
    }
}

bool RegionToolbarHandler::isAnnotationTool(ToolbarButton tool) const
{
    switch (tool) {
    case ToolbarButton::Pencil:
    case ToolbarButton::Marker:
    case ToolbarButton::Arrow:
    case ToolbarButton::Shape:
    case ToolbarButton::Text:
    case ToolbarButton::Mosaic:
    case ToolbarButton::StepBadge:
    case ToolbarButton::Eraser:
        return true;
    default:
        return false;
    }
}

void RegionToolbarHandler::saveEraserWidthAndClearHover()
{
    if (m_colorAndWidthWidget) {
        m_eraserWidth = m_colorAndWidthWidget->currentWidth();
    }
    emit eraserHoverClearRequested();
}

void RegionToolbarHandler::restoreStandardWidth()
{
    // Mosaic now uses shared width with other tools, no special handling needed
    if (m_currentTool == ToolbarButton::Eraser) {
        // Reset width section hidden state and restore standard width
        emit widthSectionHiddenRequested(false);
        emit widthRangeRequested(1, 20);
        emit currentWidthRequested(m_annotationWidth);
        if (m_toolManager) {
            m_toolManager->setWidth(m_annotationWidth);
        }
    }
}
