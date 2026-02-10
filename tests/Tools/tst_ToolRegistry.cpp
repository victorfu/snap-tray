#include <QtTest/QtTest>
#include "tools/ToolRegistry.h"
#include "tools/ToolId.h"
#include "tools/ToolDefinition.h"

/**
 * @brief Tests for ToolRegistry singleton class
 *
 * Covers:
 * - Singleton pattern
 * - Tool registration and retrieval
 * - Tool classification (drawing, action, toggle)
 * - Toolbar context (RegionSelector vs ScreenCanvas)
 * - UI visibility flags
 * - Tool capabilities
 */
class TestToolRegistry : public QObject
{
    Q_OBJECT

private slots:
    // Singleton tests
    void testSingleton_SameInstance();

    // Tool retrieval tests
    void testGet_ValidToolId();
    void testGet_InvalidToolId_ReturnsDefault();
    void testGet_AllRegisteredTools();

    // Tool classification tests
    void testIsDrawingTool_DrawingTools();
    void testIsDrawingTool_NonDrawingTools();
    void testIsActionTool_ActionTools();
    void testIsActionTool_NonActionTools();
    void testIsToggleTool_ToggleTools();
    void testIsToggleTool_NonToggleTools();
    void testIsAnnotationTool();

    // Toolbar context tests
    void testGetToolsForToolbar_RegionSelector();
    void testGetToolsForToolbar_ScreenCanvas();
    void testGetToolsForToolbar_PinWindow();
    void testGetToolsForToolbar_RegionSelectorHasDrawingTools();
    void testGetToolsForToolbar_ScreenCanvasHasToggleTools();

    // Icon and tooltip tests
    void testGetIconKey_ValidTool();
    void testGetIconKey_AllToolsHaveIcons();
    void testGetTooltip_ValidTool();
    void testGetTooltip_AllToolsHaveTooltips();
    void testGetTooltipWithShortcut_PlatformSpecificUndoRedo();

    // UI visibility tests
    void testShowColorPalette_DrawingTools();
    void testShowWidthControl_DrawingTools();
    void testShowColorWidthWidget();

    // Tool definition tests
    void testToolDefinition_HasCorrectCategory();
    void testToolDefinition_Capabilities();
    void testToolDefinition_SelectionTool();
    void testToolDefinition_PencilTool();
    void testToolDefinition_TextTool();
    void testToolDefinition_MosaicTool();

private:
    ToolRegistry& registry() { return ToolRegistry::instance(); }
};

// ============================================================================
// Singleton Tests
// ============================================================================

void TestToolRegistry::testSingleton_SameInstance()
{
    ToolRegistry& instance1 = ToolRegistry::instance();
    ToolRegistry& instance2 = ToolRegistry::instance();

    QCOMPARE(&instance1, &instance2);
}

// ============================================================================
// Tool Retrieval Tests
// ============================================================================

void TestToolRegistry::testGet_ValidToolId()
{
    const ToolDefinition& def = registry().get(ToolId::Pencil);

    QCOMPARE(def.id, ToolId::Pencil);
    QCOMPARE(def.iconKey, QString("pencil"));
    QCOMPARE(def.tooltip, QString("Pencil"));
}

void TestToolRegistry::testGet_InvalidToolId_ReturnsDefault()
{
    // Cast an out-of-range int to ToolId
    ToolId invalidId = static_cast<ToolId>(9999);
    const ToolDefinition& def = registry().get(invalidId);

    // Should return default definition (Selection)
    QCOMPARE(def.id, ToolId::Selection);
}

void TestToolRegistry::testGet_AllRegisteredTools()
{
    QList<ToolId> expectedTools = {
        ToolId::Selection, ToolId::Arrow, ToolId::Polyline,
        ToolId::Pencil, ToolId::Marker, ToolId::Shape,
        ToolId::Text, ToolId::Mosaic, ToolId::Eraser,
        ToolId::StepBadge, ToolId::EmojiSticker,
        ToolId::LaserPointer, ToolId::CursorHighlight,
        ToolId::CanvasWhiteboard, ToolId::CanvasBlackboard,
        ToolId::Undo, ToolId::Redo, ToolId::Clear,
        ToolId::Cancel, ToolId::OCR, ToolId::QRCode,
        ToolId::Pin, ToolId::Record, ToolId::Save,
        ToolId::Copy, ToolId::Exit,
        ToolId::MultiRegion, ToolId::MultiRegionDone
    };

    for (ToolId id : expectedTools) {
        const ToolDefinition& def = registry().get(id);
        QCOMPARE(def.id, id);
        QVERIFY(!def.iconKey.isEmpty());
    }
}

// ============================================================================
// Tool Classification Tests
// ============================================================================

void TestToolRegistry::testIsDrawingTool_DrawingTools()
{
    QList<ToolId> drawingTools = {
        ToolId::Pencil, ToolId::Marker, ToolId::Arrow,
        ToolId::Polyline, ToolId::Shape, ToolId::Text,
        ToolId::Mosaic, ToolId::Eraser, ToolId::StepBadge,
        ToolId::EmojiSticker
    };

    for (ToolId id : drawingTools) {
        QVERIFY2(registry().isDrawingTool(id),
                 qPrintable(QString("Expected %1 to be a drawing tool").arg(static_cast<int>(id))));
    }
}

void TestToolRegistry::testIsDrawingTool_NonDrawingTools()
{
    QList<ToolId> nonDrawingTools = {
        ToolId::Selection, ToolId::Undo, ToolId::Redo,
        ToolId::Save, ToolId::Copy, ToolId::Cancel,
        ToolId::LaserPointer, ToolId::CursorHighlight,
        ToolId::CanvasWhiteboard, ToolId::CanvasBlackboard
    };

    for (ToolId id : nonDrawingTools) {
        QVERIFY2(!registry().isDrawingTool(id),
                 qPrintable(QString("Expected %1 to NOT be a drawing tool").arg(static_cast<int>(id))));
    }
}

void TestToolRegistry::testIsActionTool_ActionTools()
{
    QList<ToolId> actionTools = {
        ToolId::Undo, ToolId::Redo, ToolId::Clear,
        ToolId::Cancel, ToolId::OCR, ToolId::QRCode,
        ToolId::Pin, ToolId::Record, ToolId::Save,
        ToolId::Copy, ToolId::Exit,
        ToolId::MultiRegion, ToolId::MultiRegionDone
    };

    for (ToolId id : actionTools) {
        QVERIFY2(registry().isActionTool(id),
                 qPrintable(QString("Expected %1 to be an action tool").arg(static_cast<int>(id))));
    }
}

void TestToolRegistry::testIsActionTool_NonActionTools()
{
    QList<ToolId> nonActionTools = {
        ToolId::Selection, ToolId::Pencil, ToolId::Marker,
        ToolId::LaserPointer, ToolId::CursorHighlight,
        ToolId::CanvasWhiteboard, ToolId::CanvasBlackboard
    };

    for (ToolId id : nonActionTools) {
        QVERIFY2(!registry().isActionTool(id),
                 qPrintable(QString("Expected %1 to NOT be an action tool").arg(static_cast<int>(id))));
    }
}

void TestToolRegistry::testIsToggleTool_ToggleTools()
{
    QList<ToolId> toggleTools = {
        ToolId::LaserPointer, ToolId::CursorHighlight,
        ToolId::CanvasWhiteboard, ToolId::CanvasBlackboard
    };

    for (ToolId id : toggleTools) {
        QVERIFY2(registry().isToggleTool(id),
                 qPrintable(QString("Expected %1 to be a toggle tool").arg(static_cast<int>(id))));
    }
}

void TestToolRegistry::testIsToggleTool_NonToggleTools()
{
    QList<ToolId> nonToggleTools = {
        ToolId::Selection, ToolId::Pencil, ToolId::Undo,
        ToolId::Save, ToolId::Mosaic
    };

    for (ToolId id : nonToggleTools) {
        QVERIFY2(!registry().isToggleTool(id),
                 qPrintable(QString("Expected %1 to NOT be a toggle tool").arg(static_cast<int>(id))));
    }
}

void TestToolRegistry::testIsAnnotationTool()
{
    // Annotation tools are drawing tools (create persistent annotations)
    QList<ToolId> annotationTools = {
        ToolId::Pencil, ToolId::Marker, ToolId::Arrow,
        ToolId::Shape, ToolId::Text, ToolId::Mosaic
    };

    for (ToolId id : annotationTools) {
        QVERIFY(registry().isAnnotationTool(id));
    }

    // Non-annotation tools
    QVERIFY(!registry().isAnnotationTool(ToolId::Selection));
    QVERIFY(!registry().isAnnotationTool(ToolId::Undo));
    QVERIFY(!registry().isAnnotationTool(ToolId::LaserPointer));
}

// ============================================================================
// Toolbar Context Tests
// ============================================================================

void TestToolRegistry::testGetToolsForToolbar_RegionSelector()
{
    QVector<ToolId> tools = registry().getToolsForToolbar(ToolbarType::RegionSelector);

    // Should include essential tools
    QVERIFY(tools.contains(ToolId::Selection));
    QVERIFY(tools.contains(ToolId::Pencil));
    QVERIFY(tools.contains(ToolId::Arrow));
    QVERIFY(tools.contains(ToolId::Shape));
    QVERIFY(tools.contains(ToolId::Text));
    QVERIFY(tools.contains(ToolId::Mosaic));
    QVERIFY(tools.contains(ToolId::Undo));
    QVERIFY(tools.contains(ToolId::Redo));
    QVERIFY(tools.contains(ToolId::QRCode));
    QVERIFY(tools.contains(ToolId::MultiRegion));
    QVERIFY(tools.contains(ToolId::Save));
    QVERIFY(tools.contains(ToolId::Copy));
}

void TestToolRegistry::testGetToolsForToolbar_ScreenCanvas()
{
    QVector<ToolId> tools = registry().getToolsForToolbar(ToolbarType::ScreenCanvas);

    // Should include canvas-specific tools
    QVERIFY(tools.contains(ToolId::Shape));
    QVERIFY(tools.contains(ToolId::Arrow));
    QVERIFY(tools.contains(ToolId::Pencil));
    QVERIFY(tools.contains(ToolId::Text));
    QVERIFY(tools.contains(ToolId::StepBadge));
    QVERIFY(tools.contains(ToolId::LaserPointer));
    QVERIFY(tools.contains(ToolId::CanvasWhiteboard));
    QVERIFY(tools.contains(ToolId::CanvasBlackboard));
    QVERIFY(tools.contains(ToolId::Clear));
    QVERIFY(tools.contains(ToolId::Exit));

    // Should NOT include RegionSelector-only tools
    QVERIFY(!tools.contains(ToolId::Save));
    QVERIFY(!tools.contains(ToolId::Copy));
    QVERIFY(!tools.contains(ToolId::Pin));
    QVERIFY(!tools.contains(ToolId::Record));
}

void TestToolRegistry::testGetToolsForToolbar_PinWindow()
{
    QVector<ToolId> tools = registry().getToolsForToolbar(ToolbarType::PinWindow);

    QVERIFY(tools.contains(ToolId::Shape));
    QVERIFY(tools.contains(ToolId::Arrow));
    QVERIFY(tools.contains(ToolId::Pencil));
    QVERIFY(tools.contains(ToolId::Marker));
    QVERIFY(tools.contains(ToolId::Text));
    QVERIFY(tools.contains(ToolId::Mosaic));
    QVERIFY(tools.contains(ToolId::Eraser));
    QVERIFY(tools.contains(ToolId::StepBadge));
    QVERIFY(tools.contains(ToolId::EmojiSticker));
    QVERIFY(tools.contains(ToolId::Undo));
    QVERIFY(tools.contains(ToolId::Redo));
    QVERIFY(tools.contains(ToolId::OCR));
    QVERIFY(tools.contains(ToolId::QRCode));
    QVERIFY(tools.contains(ToolId::Save));
    QVERIFY(tools.contains(ToolId::Copy));

    // PinWindow toolbar should not expose capture-only actions.
    QVERIFY(!tools.contains(ToolId::Selection));
    QVERIFY(!tools.contains(ToolId::Cancel));
    QVERIFY(!tools.contains(ToolId::Record));
    QVERIFY(!tools.contains(ToolId::MultiRegion));
}

void TestToolRegistry::testGetToolsForToolbar_RegionSelectorHasDrawingTools()
{
    QVector<ToolId> tools = registry().getToolsForToolbar(ToolbarType::RegionSelector);

    int drawingToolCount = 0;
    for (ToolId id : tools) {
        if (registry().isDrawingTool(id)) {
            drawingToolCount++;
        }
    }

    QVERIFY(drawingToolCount >= 5);  // At least Pencil, Marker, Arrow, Shape, Text...
}

void TestToolRegistry::testGetToolsForToolbar_ScreenCanvasHasToggleTools()
{
    QVector<ToolId> tools = registry().getToolsForToolbar(ToolbarType::ScreenCanvas);

    QVERIFY(tools.contains(ToolId::LaserPointer));
    QVERIFY(tools.contains(ToolId::CanvasWhiteboard));
    QVERIFY(tools.contains(ToolId::CanvasBlackboard));
    QVERIFY(!tools.contains(ToolId::CursorHighlight));
}

// ============================================================================
// Icon and Tooltip Tests
// ============================================================================

void TestToolRegistry::testGetIconKey_ValidTool()
{
    QCOMPARE(registry().getIconKey(ToolId::Pencil), QString("pencil"));
    QCOMPARE(registry().getIconKey(ToolId::Arrow), QString("arrow"));
    QCOMPARE(registry().getIconKey(ToolId::Shape), QString("shape"));
    QCOMPARE(registry().getIconKey(ToolId::Text), QString("text"));
    QCOMPARE(registry().getIconKey(ToolId::Mosaic), QString("mosaic"));
    QCOMPARE(registry().getIconKey(ToolId::StepBadge), QString("step-badge"));
    QCOMPARE(registry().getIconKey(ToolId::LaserPointer), QString("laser-pointer"));
    QCOMPARE(registry().getIconKey(ToolId::CanvasWhiteboard), QString("whiteboard"));
    QCOMPARE(registry().getIconKey(ToolId::CanvasBlackboard), QString("blackboard"));
}

void TestToolRegistry::testGetIconKey_AllToolsHaveIcons()
{
    QVector<ToolId> tools = registry().getToolsForToolbar(ToolbarType::RegionSelector);

    for (ToolId id : tools) {
        QString iconKey = registry().getIconKey(id);
        QVERIFY2(!iconKey.isEmpty(),
                 qPrintable(QString("Tool %1 should have an icon key").arg(static_cast<int>(id))));
    }
}

void TestToolRegistry::testGetTooltip_ValidTool()
{
    QCOMPARE(registry().getTooltip(ToolId::Pencil), QString("Pencil"));
    QCOMPARE(registry().getTooltip(ToolId::Arrow), QString("Arrow"));
    QCOMPARE(registry().getTooltip(ToolId::Selection), QString("Selection"));
    QCOMPARE(registry().getTooltip(ToolId::CanvasWhiteboard), QString("Whiteboard"));
    QCOMPARE(registry().getTooltip(ToolId::CanvasBlackboard), QString("Blackboard"));
}

void TestToolRegistry::testGetTooltip_AllToolsHaveTooltips()
{
    QVector<ToolId> tools = registry().getToolsForToolbar(ToolbarType::RegionSelector);

    for (ToolId id : tools) {
        QString tooltip = registry().getTooltip(id);
        QVERIFY2(!tooltip.isEmpty(),
                 qPrintable(QString("Tool %1 should have a tooltip").arg(static_cast<int>(id))));
    }
}

void TestToolRegistry::testGetTooltipWithShortcut_PlatformSpecificUndoRedo()
{
#ifdef Q_OS_MACOS
    QCOMPARE(registry().getTooltipWithShortcut(ToolId::Undo), QString("Undo (Cmd+Z)"));
    QCOMPARE(registry().getTooltipWithShortcut(ToolId::Redo), QString("Redo (Cmd+Shift+Z)"));
#else
    QCOMPARE(registry().getTooltipWithShortcut(ToolId::Undo), QString("Undo (Ctrl+Z)"));
    QCOMPARE(registry().getTooltipWithShortcut(ToolId::Redo), QString("Redo (Ctrl+Y)"));
#endif
    QCOMPARE(registry().getTooltipWithShortcut(ToolId::CanvasWhiteboard), QString("Whiteboard (W)"));
    QCOMPARE(registry().getTooltipWithShortcut(ToolId::CanvasBlackboard), QString("Blackboard (B)"));
}

// ============================================================================
// UI Visibility Tests
// ============================================================================

void TestToolRegistry::testShowColorPalette_DrawingTools()
{
    // Most drawing tools should show color palette
    QVERIFY(registry().showColorPalette(ToolId::Pencil));
    QVERIFY(registry().showColorPalette(ToolId::Marker));
    QVERIFY(registry().showColorPalette(ToolId::Arrow));
    QVERIFY(registry().showColorPalette(ToolId::Shape));
    QVERIFY(registry().showColorPalette(ToolId::Text));

    // Mosaic and Eraser don't show color palette
    QVERIFY(!registry().showColorPalette(ToolId::Mosaic));
    QVERIFY(!registry().showColorPalette(ToolId::Eraser));
}

void TestToolRegistry::testShowWidthControl_DrawingTools()
{
    // Stroke-based tools should show width control
    QVERIFY(registry().showWidthControl(ToolId::Pencil));
    QVERIFY(registry().showWidthControl(ToolId::Arrow));
    QVERIFY(registry().showWidthControl(ToolId::Shape));
    QVERIFY(registry().showWidthControl(ToolId::Mosaic));

    // Text doesn't need width control (uses font size)
    QVERIFY(!registry().showWidthControl(ToolId::Text));
    QVERIFY(!registry().showWidthControl(ToolId::Marker));  // Fixed width
}

void TestToolRegistry::testShowColorWidthWidget()
{
    // Combined color/width widget for main drawing tools
    QVERIFY(registry().showColorWidthWidget(ToolId::Pencil));
    QVERIFY(registry().showColorWidthWidget(ToolId::Arrow));
    QVERIFY(registry().showColorWidthWidget(ToolId::Shape));

    // Action tools don't show this
    QVERIFY(!registry().showColorWidthWidget(ToolId::Undo));
    QVERIFY(!registry().showColorWidthWidget(ToolId::Save));
}

// ============================================================================
// Tool Definition Tests
// ============================================================================

void TestToolRegistry::testToolDefinition_HasCorrectCategory()
{
    QCOMPARE(registry().get(ToolId::Selection).category, ToolCategory::Selection);
    QCOMPARE(registry().get(ToolId::Pencil).category, ToolCategory::Drawing);
    QCOMPARE(registry().get(ToolId::LaserPointer).category, ToolCategory::Toggle);
    QCOMPARE(registry().get(ToolId::CanvasWhiteboard).category, ToolCategory::Toggle);
    QCOMPARE(registry().get(ToolId::CanvasBlackboard).category, ToolCategory::Toggle);
    QCOMPARE(registry().get(ToolId::Undo).category, ToolCategory::Action);
}

void TestToolRegistry::testToolDefinition_Capabilities()
{
    // Arrow tool has color, width, and arrow style
    const ToolDefinition& arrow = registry().get(ToolId::Arrow);
    QVERIFY(arrow.hasColor);
    QVERIFY(arrow.hasWidth);
    QVERIFY(arrow.hasArrowStyle);
    QVERIFY(!arrow.hasTextFormatting);

    // Text tool has color and text formatting
    const ToolDefinition& text = registry().get(ToolId::Text);
    QVERIFY(text.hasColor);
    QVERIFY(text.hasTextFormatting);
    QVERIFY(!text.hasWidth);

    // Shape tool has color, width, shape type, and fill mode
    const ToolDefinition& shape = registry().get(ToolId::Shape);
    QVERIFY(shape.hasColor);
    QVERIFY(shape.hasWidth);
    QVERIFY(shape.hasShapeType);
    QVERIFY(shape.hasFillMode);
}

void TestToolRegistry::testToolDefinition_SelectionTool()
{
    const ToolDefinition& selection = registry().get(ToolId::Selection);

    QCOMPARE(selection.id, ToolId::Selection);
    QCOMPARE(selection.category, ToolCategory::Selection);
    QVERIFY(!selection.hasColor);
    QVERIFY(!selection.hasWidth);
    QVERIFY(!selection.isDrawingTool());
}

void TestToolRegistry::testToolDefinition_PencilTool()
{
    const ToolDefinition& pencil = registry().get(ToolId::Pencil);

    QCOMPARE(pencil.id, ToolId::Pencil);
    QCOMPARE(pencil.category, ToolCategory::Drawing);
    QVERIFY(pencil.hasColor);
    QVERIFY(pencil.hasWidth);
    QVERIFY(pencil.isDrawingTool());
    QVERIFY(!pencil.isActionTool());
}

void TestToolRegistry::testToolDefinition_TextTool()
{
    const ToolDefinition& text = registry().get(ToolId::Text);

    QCOMPARE(text.id, ToolId::Text);
    QVERIFY(text.hasColor);
    QVERIFY(text.hasTextFormatting);
    QVERIFY(!text.hasWidth);  // Text uses font size, not stroke width
}

void TestToolRegistry::testToolDefinition_MosaicTool()
{
    const ToolDefinition& mosaic = registry().get(ToolId::Mosaic);

    QCOMPARE(mosaic.id, ToolId::Mosaic);
    QVERIFY(!mosaic.hasColor);  // Mosaic doesn't use color
    QVERIFY(mosaic.hasWidth);   // But has width (blur size)
    QVERIFY(mosaic.isDrawingTool());
}

QTEST_MAIN(TestToolRegistry)
#include "tst_ToolRegistry.moc"
