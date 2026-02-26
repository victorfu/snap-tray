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

RegionToolbarHandler::RegionToolbarHandler(QObject* parent)
    : QObject(parent)
    , m_currentTool(ToolId::Selection)
    , m_stepBadgeSize(StepBadgeSize::Medium)
{
}

const std::map<ToolId, RegionToolbarHandler::ToolDispatchEntry>& RegionToolbarHandler::toolDispatchTable()
{
    static const std::map<ToolId, ToolDispatchEntry> kDispatch = {
        {ToolId::Selection, {&RegionToolbarHandler::handleSelectionTool, ToolbarButtonRole::Default, true}},
        {ToolId::Arrow, {&RegionToolbarHandler::handleAnnotationTool, ToolbarButtonRole::Default, true}},
        {ToolId::Pencil, {&RegionToolbarHandler::handleAnnotationTool, ToolbarButtonRole::Default, true}},
        {ToolId::Marker, {&RegionToolbarHandler::handleAnnotationTool, ToolbarButtonRole::Default, true}},
        {ToolId::Shape, {&RegionToolbarHandler::handleAnnotationTool, ToolbarButtonRole::Default, true}},
        {ToolId::Text, {&RegionToolbarHandler::handleAnnotationTool, ToolbarButtonRole::Default, true}},
        {ToolId::Eraser, {&RegionToolbarHandler::handleAnnotationTool, ToolbarButtonRole::Default, true}},
        {ToolId::EmojiSticker, {&RegionToolbarHandler::handleAnnotationTool, ToolbarButtonRole::Default, true}},
        {ToolId::StepBadge, {&RegionToolbarHandler::handleStepBadgeTool, ToolbarButtonRole::Default, true}},
        {ToolId::Mosaic, {&RegionToolbarHandler::handleMosaicTool, ToolbarButtonRole::Default, true}},
        {ToolId::Undo, {&RegionToolbarHandler::handleActionButton, ToolbarButtonRole::Default, false}},
        {ToolId::Redo, {&RegionToolbarHandler::handleActionButton, ToolbarButtonRole::Default, false}},
        {ToolId::Cancel, {&RegionToolbarHandler::handleActionButton, ToolbarButtonRole::Cancel, false}},
        {ToolId::OCR, {&RegionToolbarHandler::handleActionButton, ToolbarButtonRole::Default, false}},
        {ToolId::QRCode, {&RegionToolbarHandler::handleActionButton, ToolbarButtonRole::Default, false}},
        {ToolId::Pin, {&RegionToolbarHandler::handleActionButton, ToolbarButtonRole::Action, false}},
        {ToolId::Record, {&RegionToolbarHandler::handleActionButton, ToolbarButtonRole::Record, false}},
        {ToolId::Share, {&RegionToolbarHandler::handleActionButton, ToolbarButtonRole::Action, false}},
        {ToolId::Save, {&RegionToolbarHandler::handleActionButton, ToolbarButtonRole::Action, false}},
        {ToolId::Compose, {&RegionToolbarHandler::handleActionButton, ToolbarButtonRole::Action, false}},
        {ToolId::Copy, {&RegionToolbarHandler::handleActionButton, ToolbarButtonRole::Action, false}},
        {ToolId::MultiRegion, {&RegionToolbarHandler::handleMultiRegionToggle, ToolbarButtonRole::Default, false}},
        {ToolId::MultiRegionDone, {&RegionToolbarHandler::handleMultiRegionDone, ToolbarButtonRole::Default, false}},
    };
    return kDispatch;
}

const std::map<ToolId, RegionToolbarHandler::ClickHandler>& RegionToolbarHandler::actionDispatchTable()
{
    static const std::map<ToolId, ClickHandler> kActionDispatch = {
        {ToolId::Undo, &RegionToolbarHandler::handleUndoAction},
        {ToolId::Redo, &RegionToolbarHandler::handleRedoAction},
        {ToolId::Cancel, &RegionToolbarHandler::handleCancelAction},
        {ToolId::OCR, &RegionToolbarHandler::handleOcrAction},
        {ToolId::QRCode, &RegionToolbarHandler::handleQrCodeAction},
        {ToolId::Pin, &RegionToolbarHandler::handlePinAction},
        {ToolId::Record, &RegionToolbarHandler::handleRecordAction},
        {ToolId::Share, &RegionToolbarHandler::handleShareAction},
        {ToolId::Save, &RegionToolbarHandler::handleSaveAction},
        {ToolId::Compose, &RegionToolbarHandler::handleComposeAction},
        {ToolId::Copy, &RegionToolbarHandler::handleCopyAction},
    };
    return kActionDispatch;
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
        const auto& dispatch = toolDispatchTable();
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
        auto cancelDispatchIt = dispatch.find(ToolId::Cancel);
        if (cancelDispatchIt != dispatch.end() &&
            cancelDispatchIt->second.buttonRole == ToolbarButtonRole::Cancel) {
            cancelBtn.cancel();
        }
        buttons.append(cancelBtn);

        m_toolbar->setButtons(buttons);
        m_toolbar->setActiveButtonIds({});
        m_toolbar->setIconColorProvider([this](int buttonId, bool isActive, bool isHovered) {
            return getToolbarIconColor(buttonId, isActive, isHovered);
        });
        return;
    }

    const QVector<ToolId> toolbarTools = registry.getToolsForToolbar(ToolbarType::RegionSelector);
    const auto& dispatch = toolDispatchTable();
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
        const auto dispatchIt = dispatch.find(toolId);
        const ToolbarButtonRole role = dispatchIt != dispatch.end()
            ? dispatchIt->second.buttonRole
            : (def.category == ToolCategory::Toggle ? ToolbarButtonRole::Toggle : ToolbarButtonRole::Default);
        switch (role) {
        case ToolbarButtonRole::Cancel:
            config.cancel();
            break;
        case ToolbarButtonRole::Record:
            config.record();
            break;
        case ToolbarButtonRole::Action:
            config.action();
            break;
        case ToolbarButtonRole::Toggle:
            config.toggle();
            break;
        case ToolbarButtonRole::Default:
            break;
        }

        buttons.append(config);

        const bool supportsActiveState = dispatchIt != dispatch.end()
            ? dispatchIt->second.supportsActiveState
            : (toolId == ToolId::Selection || isAnnotationTool(toolId));
        if (supportsActiveState) {
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

    if (btn == ToolId::Share && m_shareInProgress) {
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
    const auto& dispatch = toolDispatchTable();
    auto it = dispatch.find(button);
    if (it == dispatch.end() || !it->second.handler) {
        return;
    }
    (this->*(it->second.handler))(button);
}

void RegionToolbarHandler::handleSelectionTool(ToolId button)
{
    // Selection has no sub-toolbar, just switch tool
    m_currentTool = button;
    m_showSubToolbar = true;
    emit showSizeSectionRequested(false);
    emit toolChanged(m_currentTool, m_showSubToolbar);
    emit updateRequested();
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
    emit toolChanged(m_currentTool, m_showSubToolbar);
    emit updateRequested();
}

void RegionToolbarHandler::handleStepBadgeTool(ToolId button)
{
    if (m_currentTool == button) {
        // Same tool clicked - switch to Selection tool
        m_currentTool = ToolId::Selection;
        m_showSubToolbar = true;
        emit showSizeSectionRequested(false);
        emit toolChanged(m_currentTool, m_showSubToolbar);
        emit updateRequested();
        return;
    } else {
        // Different tool - select it and show sub-toolbar
        m_currentTool = button;
        if (m_toolManager) {
            m_toolManager->setCurrentTool(button);
        }
        m_showSubToolbar = true;
    }
    // Show size section, hide width section for step badge
    emit showSizeSectionRequested(true);
    emit showWidthSectionRequested(false);
    emit stepBadgeSizeRequested(m_stepBadgeSize);
    // StepBadgeToolHandler reads size from AnnotationSettingsManager, no setWidth needed
    emit toolChanged(m_currentTool, m_showSubToolbar);
    emit updateRequested();
}

void RegionToolbarHandler::handleMosaicTool(ToolId button)
{
    if (m_currentTool == button) {
        // Same tool clicked - switch to Selection tool
        m_currentTool = ToolId::Selection;
        m_showSubToolbar = true;
        emit showColorSectionRequested(true);  // Restore color section visibility
        emit toolChanged(m_currentTool, m_showSubToolbar);
        emit updateRequested();
        return;
    } else {
        // Different tool - select it and show sub-toolbar
        m_currentTool = button;
        if (m_toolManager) {
            m_toolManager->setCurrentTool(button);
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
    emit toolChanged(m_currentTool, m_showSubToolbar);
    emit updateRequested();
}

void RegionToolbarHandler::handleActionButton(ToolId button)
{
    const auto& actions = actionDispatchTable();
    auto it = actions.find(button);
    if (it == actions.end() || !it->second) {
        return;
    }
    (this->*(it->second))(button);
}

void RegionToolbarHandler::handleUndoAction(ToolId)
{
    if (m_annotationLayer && m_annotationLayer->canUndo()) {
        emit undoRequested();
        emit updateRequested();
    }
}

void RegionToolbarHandler::handleRedoAction(ToolId)
{
    if (m_annotationLayer && m_annotationLayer->canRedo()) {
        emit redoRequested();
        emit updateRequested();
    }
}

void RegionToolbarHandler::handleCancelAction(ToolId)
{
    if (m_multiRegionMode) {
        emit multiRegionCancelRequested();
        return;
    }
    emit cancelRequested();
}

void RegionToolbarHandler::handleOcrAction(ToolId)
{
    emit ocrRequested();
}

void RegionToolbarHandler::handleQrCodeAction(ToolId)
{
    emit qrCodeRequested();
}

void RegionToolbarHandler::handlePinAction(ToolId)
{
    emit pinRequested();
}

void RegionToolbarHandler::handleRecordAction(ToolId)
{
    emit recordRequested();
}

void RegionToolbarHandler::handleShareAction(ToolId)
{
    if (m_shareInProgress) {
        return;
    }
    emit shareRequested();
}

void RegionToolbarHandler::handleSaveAction(ToolId)
{
    emit saveRequested();
}

void RegionToolbarHandler::handleComposeAction(ToolId)
{
    emit composeRequested();
}

void RegionToolbarHandler::handleCopyAction(ToolId)
{
    emit copyRequested();
}

void RegionToolbarHandler::handleMultiRegionToggle(ToolId)
{
    emit multiRegionToggled(!m_multiRegionMode);
}

void RegionToolbarHandler::handleMultiRegionDone(ToolId)
{
    emit multiRegionDoneRequested();
}

bool RegionToolbarHandler::isAnnotationTool(ToolId tool) const
{
    return ToolTraits::isAnnotationTool(tool);
}
