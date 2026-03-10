#include "region/RegionToolbarHandler.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/StepBadgeAnnotation.h"
#include "tools/ToolManager.h"
#include "tools/ToolId.h"

RegionToolbarHandler::RegionToolbarHandler(QObject* parent)
    : QObject(parent)
    , m_currentTool(ToolId::Selection)
    , m_stepBadgeSize(StepBadgeSize::Medium)
{
}

const std::map<ToolId, RegionToolbarHandler::ToolDispatchEntry>& RegionToolbarHandler::toolDispatchTable()
{
    static const std::map<ToolId, ToolDispatchEntry> kDispatch = {
        {ToolId::Selection, {&RegionToolbarHandler::handleSelectionTool}},
        {ToolId::Arrow, {&RegionToolbarHandler::handleAnnotationTool}},
        {ToolId::Pencil, {&RegionToolbarHandler::handleAnnotationTool}},
        {ToolId::Marker, {&RegionToolbarHandler::handleAnnotationTool}},
        {ToolId::Shape, {&RegionToolbarHandler::handleAnnotationTool}},
        {ToolId::Text, {&RegionToolbarHandler::handleAnnotationTool}},
        {ToolId::Eraser, {&RegionToolbarHandler::handleAnnotationTool}},
        {ToolId::EmojiSticker, {&RegionToolbarHandler::handleAnnotationTool}},
        {ToolId::StepBadge, {&RegionToolbarHandler::handleStepBadgeTool}},
        {ToolId::Mosaic, {&RegionToolbarHandler::handleMosaicTool}},
        {ToolId::Undo, {&RegionToolbarHandler::handleActionButton}},
        {ToolId::Redo, {&RegionToolbarHandler::handleActionButton}},
        {ToolId::Cancel, {&RegionToolbarHandler::handleActionButton}},
        {ToolId::OCR, {&RegionToolbarHandler::handleActionButton}},
        {ToolId::QRCode, {&RegionToolbarHandler::handleActionButton}},
        {ToolId::Pin, {&RegionToolbarHandler::handleActionButton}},
        {ToolId::Record, {&RegionToolbarHandler::handleActionButton}},
        {ToolId::Share, {&RegionToolbarHandler::handleActionButton}},
        {ToolId::Save, {&RegionToolbarHandler::handleActionButton}},
        {ToolId::Copy, {&RegionToolbarHandler::handleActionButton}},
        {ToolId::MultiRegion, {&RegionToolbarHandler::handleMultiRegionToggle}},
        {ToolId::MultiRegionDone, {&RegionToolbarHandler::handleMultiRegionDone}},
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
    m_currentTool = button;
    m_showSubToolbar = true;
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
    emit toolChanged(m_currentTool, m_showSubToolbar);
    emit updateRequested();
}

void RegionToolbarHandler::handleStepBadgeTool(ToolId button)
{
    if (m_currentTool == button) {
        m_currentTool = ToolId::Selection;
        m_showSubToolbar = true;
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
    emit toolChanged(m_currentTool, m_showSubToolbar);
    emit updateRequested();
}

void RegionToolbarHandler::handleMosaicTool(ToolId button)
{
    if (m_currentTool == button) {
        m_currentTool = ToolId::Selection;
        m_showSubToolbar = true;
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
    if (m_toolManager) {
        m_toolManager->setWidth(m_annotationWidth);
    }
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
