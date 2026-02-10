#include "region/RegionToolbarHandler.h"
#include "region/SelectionStateManager.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/StepBadgeAnnotation.h"
#include "tools/ToolManager.h"
#include "tools/ToolId.h"
#include "toolbar/ToolbarCore.h"
#include "toolbar/ToolOptionsPanel.h"
#include "IconRenderer.h"
#include "OCRManager.h"
#include "PlatformFeatures.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolTraits.h"

#include <QWidget>
#include <QDebug>

RegionToolbarHandler::RegionToolbarHandler(QObject* parent)
    : QObject(parent)
    , m_currentTool(ToolId::Selection)
    , m_stepBadgeSize(StepBadgeSize::Medium)
{
}

void RegionToolbarHandler::setToolbar(ToolbarCore* toolbar)
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

void RegionToolbarHandler::setColorAndWidthWidget(ToolOptionsPanel* widget)
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
    auto& registry = ToolRegistry::instance();
    auto loadToolIcon = [&](ToolId toolId) {
        const QString iconKey = registry.getIconKey(toolId);
        if (!iconKey.isEmpty()) {
            iconRenderer.loadIconByKey(iconKey);
        }
    };
    for (ToolId toolId : registry.getToolsForToolbar(ToolbarType::RegionSelector)) {
        // Keep OCR icon lazy with feature availability, same as prior behavior.
        if (toolId == ToolId::OCR && !PlatformFeatures::instance().isOCRAvailable()) {
            continue;
        }
        loadToolIcon(toolId);
    }
    loadToolIcon(ToolId::MultiRegionDone);

    // Shape and arrow style icons for ToolOptionsPanel sections
    iconRenderer.loadIconsByKey({
        "rectangle",
        "ellipse",
        "shape-filled",
        "shape-outline",
        "arrow-none",
        "arrow-end",
        "arrow-end-outline",
        "arrow-end-line",
        "arrow-both",
        "arrow-both-outline",
        "auto-blur"
    });

    // Configure buttons
    QVector<Toolbar::ButtonConfig> buttons;
    if (m_multiRegionMode) {
        const auto& doneDef = registry.get(ToolId::MultiRegionDone);
        buttons.append({
            static_cast<int>(ToolId::MultiRegionDone),
            doneDef.iconKey,
            registry.getTooltipWithShortcut(ToolId::MultiRegionDone),
            doneDef.showSeparatorBefore
        });

        const auto& cancelDef = registry.get(ToolId::Cancel);
        Toolbar::ButtonConfig cancelBtn(
            static_cast<int>(ToolId::Cancel),
            cancelDef.iconKey,
            registry.getTooltipWithShortcut(ToolId::Cancel),
            cancelDef.showSeparatorBefore);
        cancelBtn.cancel();
        buttons.append(cancelBtn);

        m_toolbar->setButtons(buttons);
        m_toolbar->setActiveButtonIds({});
        m_toolbar->setIconColorProvider([this](int buttonId, bool isActive, bool isHovered) {
            return getToolbarIconColor(buttonId, isActive, isHovered);
        });
        return;
    }

    const QVector<ToolId> toolbarTools = registry.getToolsForToolbar(ToolbarType::RegionSelector);
    QVector<int> activeButtonIds;
    for (ToolId toolId : toolbarTools) {
        if (toolId == ToolId::OCR && !PlatformFeatures::instance().isOCRAvailable()) {
            continue;
        }

        const auto& def = registry.get(toolId);
        Toolbar::ButtonConfig config(
            static_cast<int>(toolId),
            def.iconKey,
            registry.getTooltipWithShortcut(toolId),
            def.showSeparatorBefore);

        if (toolId == ToolId::Cancel) {
            config.cancel();
        } else if (toolId == ToolId::Record) {
            config.record();
        } else if (toolId == ToolId::Pin || toolId == ToolId::Save || toolId == ToolId::Copy) {
            config.action();
        } else if (def.category == ToolCategory::Toggle) {
            config.toggle();
        }

        buttons.append(config);

        if (toolId == ToolId::Selection || isAnnotationTool(toolId)) {
            activeButtonIds.append(static_cast<int>(toolId));
        }
    }

    m_toolbar->setButtons(buttons);
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
    ToolId btn = static_cast<ToolId>(buttonId);

    // Show gray for unavailable features
    if (btn == ToolId::OCR && !PlatformFeatures::instance().isOCRAvailable()) {
        return QColor(128, 128, 128);
    }

    // Show yellow when processing
    if (btn == ToolId::OCR && m_ocrInProgress) {
        return QColor(255, 200, 100);
    }

    if (btn == ToolId::MultiRegionDone) {
        return style.iconActionColor;
    }

    // Show gray for Undo when nothing to undo
    if (btn == ToolId::Undo && m_annotationLayer && !m_annotationLayer->canUndo()) {
        return QColor(128, 128, 128);
    }

    // Show gray for Redo when nothing to redo
    if (btn == ToolId::Redo && m_annotationLayer && !m_annotationLayer->canRedo()) {
        return QColor(128, 128, 128);
    }

    // All icons use the same color scheme as Pencil
    if (isActive) {
        return style.iconActiveColor;
    }
    return style.iconNormalColor;
}

void RegionToolbarHandler::handleToolbarClick(ToolId button)
{
    switch (button) {
    case ToolId::Selection:
        // Selection has no sub-toolbar, just switch tool
        m_currentTool = button;
        m_showSubToolbar = true;
        emit showSizeSectionRequested(false);
        qDebug() << "Tool selected:" << static_cast<int>(button);
        emit toolChanged(m_currentTool, m_showSubToolbar);
        emit updateRequested();
        break;

    case ToolId::Arrow:
    case ToolId::Pencil:
    case ToolId::Marker:
    case ToolId::Shape:
    case ToolId::Text:
    case ToolId::Eraser:
    case ToolId::EmojiSticker:
        handleAnnotationTool(button);
        break;

    case ToolId::StepBadge:
        handleStepBadgeTool();
        break;

    case ToolId::Mosaic:
        handleMosaicTool();
        break;

    case ToolId::Undo:
    case ToolId::Redo:
    case ToolId::Cancel:
    case ToolId::OCR:
    case ToolId::QRCode:
    case ToolId::Pin:
    case ToolId::Record:
    case ToolId::Save:
    case ToolId::Copy:
        handleActionButton(button);
        break;

    case ToolId::MultiRegion:
        emit multiRegionToggled(!m_multiRegionMode);
        break;

    case ToolId::MultiRegionDone:
        emit multiRegionDoneRequested();
        break;

    default:
        break;
    }
}

void RegionToolbarHandler::handleAnnotationTool(ToolId button)
{
    if (m_currentTool == button) {
        // Same tool clicked - switch to Selection tool
        m_currentTool = ToolId::Selection;
        m_showSubToolbar = true;
    } else {
        // Different tool - select it and show sub-toolbar
        m_currentTool = button;
        m_showSubToolbar = true;
    }
    // Update ToolManager for all annotation tools (not just Eraser)
    // This triggers CursorManager::updateToolCursor() via toolChanged signal
    if (m_toolManager) {
        m_toolManager->setCurrentTool(m_currentTool);
    }
    emit showSizeSectionRequested(false);
    qDebug() << "Tool selected:" << static_cast<int>(button) << "showSubToolbar:" << m_showSubToolbar;
    emit toolChanged(m_currentTool, m_showSubToolbar);
    emit updateRequested();
}

void RegionToolbarHandler::handleStepBadgeTool()
{
    if (m_currentTool == ToolId::StepBadge) {
        // Same tool clicked - switch to Selection tool
        m_currentTool = ToolId::Selection;
        m_showSubToolbar = true;
        emit showSizeSectionRequested(false);
        emit toolChanged(m_currentTool, m_showSubToolbar);
        emit updateRequested();
        return;
    } else {
        // Different tool - select it and show sub-toolbar
        m_currentTool = ToolId::StepBadge;
        if (m_toolManager) {
            m_toolManager->setCurrentTool(ToolId::StepBadge);
        }
        m_showSubToolbar = true;
    }
    // Show size section, hide width section for step badge
    emit showSizeSectionRequested(true);
    emit showWidthSectionRequested(false);
    emit stepBadgeSizeRequested(m_stepBadgeSize);
    // StepBadgeToolHandler reads size from AnnotationSettingsManager, no setWidth needed
    qDebug() << "StepBadge tool selected, showSubToolbar:" << m_showSubToolbar;
    emit toolChanged(m_currentTool, m_showSubToolbar);
    emit updateRequested();
}

void RegionToolbarHandler::handleMosaicTool()
{
    if (m_currentTool == ToolId::Mosaic) {
        // Same tool clicked - switch to Selection tool
        m_currentTool = ToolId::Selection;
        m_showSubToolbar = true;
        emit showColorSectionRequested(true);  // Restore color section visibility
        emit toolChanged(m_currentTool, m_showSubToolbar);
        emit updateRequested();
        return;
    } else {
        // Different tool - select it and show sub-toolbar
        m_currentTool = ToolId::Mosaic;
        if (m_toolManager) {
            m_toolManager->setCurrentTool(ToolId::Mosaic);
        }
        m_showSubToolbar = true;
    }
    // Use shared WidthSection for Mosaic (synced with other tools)
    emit showWidthSectionRequested(true);
    emit widthSectionHiddenRequested(false);
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

void RegionToolbarHandler::handleActionButton(ToolId button)
{
    switch (button) {
    case ToolId::Undo:
        if (m_annotationLayer && m_annotationLayer->canUndo()) {
            emit undoRequested();
            qDebug() << "Undo";
            emit updateRequested();
        }
        break;

    case ToolId::Redo:
        if (m_annotationLayer && m_annotationLayer->canRedo()) {
            emit redoRequested();
            qDebug() << "Redo";
            emit updateRequested();
        }
        break;

    case ToolId::Cancel:
        if (m_multiRegionMode) {
            emit multiRegionCancelRequested();
        } else {
            emit cancelRequested();
        }
        break;

    case ToolId::OCR:
        emit ocrRequested();
        break;

    case ToolId::QRCode:
        emit qrCodeRequested();
        break;

    case ToolId::Pin:
        emit pinRequested();
        break;

    case ToolId::Record:
        emit recordRequested();
        break;

    case ToolId::Save:
        emit saveRequested();
        break;

    case ToolId::Copy:
        emit copyRequested();
        break;

    default:
        break;
    }
}

bool RegionToolbarHandler::isAnnotationTool(ToolId tool) const
{
    return ToolTraits::isAnnotationTool(tool);
}
