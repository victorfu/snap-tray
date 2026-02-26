#ifndef REGIONTOOLBARHANDLER_H
#define REGIONTOOLBARHANDLER_H

#include <QObject>
#include <QColor>
#include <QCursor>
#include <map>
#include "tools/ToolId.h"

class QWidget;
class ToolbarCore;
class ToolManager;
class AnnotationLayer;
class ToolOptionsPanel;
class SelectionStateManager;
class OCRManager;

enum class LineEndStyle;
enum class LineStyle;
enum class ShapeType;
enum class ShapeFillMode;
enum class StepBadgeSize;

/**
 * @brief Handles toolbar setup and button click actions for RegionSelector.
 *
 * Manages toolbar button configuration, icon loading, icon color logic,
 * and click handling. Uses signals to communicate state changes back to
 * RegionSelector since many operations require updating parent state.
 */
class RegionToolbarHandler : public QObject
{
    Q_OBJECT

public:
    explicit RegionToolbarHandler(QObject* parent = nullptr);

    // Dependency injection
    void setToolbar(ToolbarCore* toolbar);
    void setToolManager(ToolManager* manager);
    void setAnnotationLayer(AnnotationLayer* layer);
    void setColorAndWidthWidget(ToolOptionsPanel* widget);
    void setSelectionManager(SelectionStateManager* manager);
    void setOCRManager(OCRManager* manager);
    void setParentWidget(QWidget* widget);

    // Setup toolbar buttons (call once during initialization)
    void setupToolbarButtons();

    // Handle toolbar button click
    void handleToolbarClick(ToolId button);

    // Icon color provider (passed to ToolbarCore)
    QColor getToolbarIconColor(int buttonId, bool isActive, bool isHovered) const;

    // Current state
    ToolId currentTool() const { return m_currentTool; }
    bool showSubToolbar() const { return m_showSubToolbar; }
    int annotationWidth() const { return m_annotationWidth; }

    // State setters (synced from RegionSelector before handle calls)
    void setCurrentTool(ToolId tool) { m_currentTool = tool; }
    void setShowSubToolbar(bool show) { m_showSubToolbar = show; }
    void setAnnotationWidth(int width) { m_annotationWidth = width; }
    void setAnnotationColor(const QColor& color) { m_annotationColor = color; }
    void setStepBadgeSize(StepBadgeSize size);
    void setOCRInProgress(bool inProgress) { m_ocrInProgress = inProgress; }
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
    void recordRequested();
    void saveRequested();
    void composeRequested();
    void copyRequested();
    void shareRequested();
    void ocrRequested();
    void qrCodeRequested();
    void multiRegionToggled(bool enabled);
    void multiRegionDoneRequested();
    void multiRegionCancelRequested();

    // ToolOptionsPanel configuration signals
    void showSizeSectionRequested(bool show);
    void showWidthSectionRequested(bool show);
    void widthSectionHiddenRequested(bool hidden);
    void showColorSectionRequested(bool show);
    void widthRangeRequested(int min, int max);
    void currentWidthRequested(int width);
    void stepBadgeSizeRequested(StepBadgeSize size);

private:
    enum class ToolbarButtonRole {
        Default,
        Toggle,
        Action,
        Record,
        Cancel
    };

    using ClickHandler = void (RegionToolbarHandler::*)(ToolId);

    struct ToolDispatchEntry {
        ClickHandler handler = nullptr;
        ToolbarButtonRole buttonRole = ToolbarButtonRole::Default;
        bool supportsActiveState = false;
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
    void handleRecordAction(ToolId button);
    void handleShareAction(ToolId button);
    void handleSaveAction(ToolId button);
    void handleComposeAction(ToolId button);
    void handleCopyAction(ToolId button);
    void handleMultiRegionToggle(ToolId button);
    void handleMultiRegionDone(ToolId button);

    // Tool state helpers
    bool isAnnotationTool(ToolId tool) const;
    void restoreStandardWidth();

    // Dependencies (non-owning pointers)
    ToolbarCore* m_toolbar = nullptr;
    ToolManager* m_toolManager = nullptr;
    AnnotationLayer* m_annotationLayer = nullptr;
    ToolOptionsPanel* m_colorAndWidthWidget = nullptr;
    SelectionStateManager* m_selectionManager = nullptr;
    OCRManager* m_ocrManager = nullptr;
    QWidget* m_parentWidget = nullptr;

    // State
    ToolId m_currentTool;
    bool m_showSubToolbar = true;
    QColor m_annotationColor = Qt::red;
    int m_annotationWidth = 3;
    StepBadgeSize m_stepBadgeSize;
    bool m_ocrInProgress = false;
    bool m_shareInProgress = false;
    bool m_multiRegionMode = false;
};

#endif // REGIONTOOLBARHANDLER_H
