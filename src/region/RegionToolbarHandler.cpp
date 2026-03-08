#include "region/RegionToolbarHandler.h"
#include "region/SelectionStateManager.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/StepBadgeAnnotation.h"
#include "tools/ToolManager.h"
#include "tools/ToolId.h"
#include "OCRManager.h"
#include "PlatformFeatures.h"
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
        {ToolId::Copy, &RegionToolbarHandler::handleCopyAction},
    };
    return kActionDispatch;
}

void RegionToolbarHandler::setToolManager(ToolManager* manager)
{
    m_toolManager = manager;
}

void RegionToolbarHandler::setAnnotationLayer(AnnotationLayer* layer)
{
    m_annotationLayer = layer;
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
