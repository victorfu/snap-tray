#ifndef REGIONTOOLBARHANDLER_H
#define REGIONTOOLBARHANDLER_H

#include <QObject>
#include <QColor>
#include <QCursor>
#include <functional>

class QWidget;
class ToolbarWidget;
class ToolManager;
class AnnotationLayer;
class ColorAndWidthWidget;
class SelectionStateManager;
class OCRManager;

enum class ToolbarButton;
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
    void setToolbar(ToolbarWidget* toolbar);
    void setToolManager(ToolManager* manager);
    void setAnnotationLayer(AnnotationLayer* layer);
    void setColorAndWidthWidget(ColorAndWidthWidget* widget);
    void setSelectionManager(SelectionStateManager* manager);
    void setOCRManager(OCRManager* manager);
    void setParentWidget(QWidget* widget);

    // Setup toolbar buttons (call once during initialization)
    void setupToolbarButtons();

    // Handle toolbar button click
    void handleToolbarClick(ToolbarButton button);

    // Icon color provider (passed to ToolbarWidget)
    QColor getToolbarIconColor(int buttonId, bool isActive, bool isHovered) const;

    // Current state
    ToolbarButton currentTool() const { return m_currentTool; }
    bool showSubToolbar() const { return m_showSubToolbar; }
    int eraserWidth() const { return m_eraserWidth; }
    int annotationWidth() const { return m_annotationWidth; }

    // State setters (synced from RegionSelector before handle calls)
    void setCurrentTool(ToolbarButton tool) { m_currentTool = tool; }
    void setShowSubToolbar(bool show) { m_showSubToolbar = show; }
    void setEraserWidth(int width) { m_eraserWidth = width; }
    void setAnnotationWidth(int width) { m_annotationWidth = width; }
    void setAnnotationColor(const QColor& color) { m_annotationColor = color; }
    void setStepBadgeSize(StepBadgeSize size);
    void setOCRInProgress(bool inProgress) { m_ocrInProgress = inProgress; }
    void setMultiRegionMode(bool enabled) { m_multiRegionMode = enabled; }

signals:
    // Tool state changes
    void toolChanged(ToolbarButton tool, bool showSubToolbar);
    void cursorChangeRequested(Qt::CursorShape cursor);
    void updateRequested();

    // Action button signals
    void undoRequested();
    void redoRequested();
    void cancelRequested();
    void pinRequested();
    void recordRequested();
    void scrollCaptureRequested();
    void saveRequested();
    void copyRequested();
    void ocrRequested();
    void multiRegionToggled(bool enabled);
    void multiRegionDoneRequested();
    void multiRegionCancelRequested();

    // ColorAndWidthWidget configuration signals
    void showSizeSectionRequested(bool show);
    void showWidthSectionRequested(bool show);
    void widthSectionHiddenRequested(bool hidden);
    void showColorSectionRequested(bool show);
    void widthRangeRequested(int min, int max);
    void currentWidthRequested(int width);
    void stepBadgeSizeRequested(StepBadgeSize size);

    // Tool-specific signals
    void eraserHoverClearRequested();

private:
    // Tool switching helpers
    void handleAnnotationTool(ToolbarButton button);
    void handleStepBadgeTool();
    void handleMosaicTool();
    void handleEraserTool();
    void handleActionButton(ToolbarButton button);

    // Tool state helpers
    bool isAnnotationTool(ToolbarButton tool) const;
    void saveEraserWidthAndClearHover();
    void restoreStandardWidth();

    // Dependencies (non-owning pointers)
    ToolbarWidget* m_toolbar = nullptr;
    ToolManager* m_toolManager = nullptr;
    AnnotationLayer* m_annotationLayer = nullptr;
    ColorAndWidthWidget* m_colorAndWidthWidget = nullptr;
    SelectionStateManager* m_selectionManager = nullptr;
    OCRManager* m_ocrManager = nullptr;
    QWidget* m_parentWidget = nullptr;

    // State
    ToolbarButton m_currentTool;
    bool m_showSubToolbar = true;
    QColor m_annotationColor = Qt::red;
    int m_annotationWidth = 3;
    int m_eraserWidth = 20;
    StepBadgeSize m_stepBadgeSize;
    bool m_ocrInProgress = false;
    bool m_multiRegionMode = false;
};

#endif // REGIONTOOLBARHANDLER_H
