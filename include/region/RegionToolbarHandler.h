#ifndef REGIONTOOLBARHANDLER_H
#define REGIONTOOLBARHANDLER_H

#include <QObject>
#include <map>
#include "tools/ToolId.h"

class ToolManager;
class AnnotationLayer;
enum class StepBadgeSize;

/**
 * @brief Routes RegionSelector toolbar actions to the host widget state.
 *
 * The QML toolbar owns presentation. This handler only keeps the interaction
 * rules for tool toggles and action buttons in one place.
 */
class RegionToolbarHandler : public QObject
{
    Q_OBJECT

public:
    explicit RegionToolbarHandler(QObject* parent = nullptr);

    void setToolManager(ToolManager* manager);
    void setAnnotationLayer(AnnotationLayer* layer);

    void handleToolbarClick(ToolId button);

    // State setters (synced from RegionSelector before handle calls)
    void setCurrentTool(ToolId tool) { m_currentTool = tool; }
    void setShowSubToolbar(bool show) { m_showSubToolbar = show; }
    void setAnnotationWidth(int width) { m_annotationWidth = width; }
    void setStepBadgeSize(StepBadgeSize size);
    void setShareInProgress(bool inProgress) { m_shareInProgress = inProgress; }
    void setMultiRegionMode(bool enabled) { m_multiRegionMode = enabled; }

signals:
    // Tool state changes
    void toolChanged(ToolId tool, bool showSubToolbar);
    void updateRequested();

    // Action button signals
    void undoRequested();
    void redoRequested();
    void cancelRequested();
    void pinRequested();
    void saveRequested();
    void copyRequested();
    void shareRequested();
    void ocrRequested();
    void qrCodeRequested();
    void multiRegionToggled(bool enabled);
    void multiRegionDoneRequested();
    void multiRegionCancelRequested();

private:
    using ClickHandler = void (RegionToolbarHandler::*)(ToolId);

    struct ToolDispatchEntry {
        ClickHandler handler = nullptr;
    };

    static const std::map<ToolId, ToolDispatchEntry>& toolDispatchTable();
    static const std::map<ToolId, ClickHandler>& actionDispatchTable();

    // Click handlers
    void handleSelectionTool(ToolId button);
    // Tool switching helpers
    void handleAnnotationTool(ToolId button);
    void handleStepBadgeTool(ToolId button);
    void handleMosaicTool(ToolId button);
    void handleActionButton(ToolId button);
    void handleUndoAction(ToolId button);
    void handleRedoAction(ToolId button);
    void handleCancelAction(ToolId button);
    void handleOcrAction(ToolId button);
    void handleQrCodeAction(ToolId button);
    void handlePinAction(ToolId button);
    void handleShareAction(ToolId button);
    void handleSaveAction(ToolId button);
    void handleCopyAction(ToolId button);
    void handleMultiRegionToggle(ToolId button);
    void handleMultiRegionDone(ToolId button);

    ToolManager* m_toolManager = nullptr;
    AnnotationLayer* m_annotationLayer = nullptr;

    ToolId m_currentTool;
    bool m_showSubToolbar = true;
    int m_annotationWidth = 3;
    StepBadgeSize m_stepBadgeSize;
    bool m_shareInProgress = false;
    bool m_multiRegionMode = false;
};

#endif // REGIONTOOLBARHANDLER_H
