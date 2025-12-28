#include "tools/ToolRegistry.h"

ToolRegistry& ToolRegistry::instance() {
    static ToolRegistry registry;
    return registry;
}

ToolRegistry::ToolRegistry() {
    // Initialize default definition for unknown tools
    m_defaultDefinition.id = ToolId::Selection;
    m_defaultDefinition.iconKey = "selection";
    m_defaultDefinition.tooltip = "Unknown Tool";
    m_defaultDefinition.category = ToolCategory::Selection;

    registerTools();
}

void ToolRegistry::registerTools() {
    // Selection tool
    registerTool({
        ToolId::Selection,
        "selection",
        "Selection",
        "",
        ToolCategory::Selection,
        false, false, false, false, false, false,  // capabilities
        false, QColor()  // UI config
    });

    // Drawing tools
    registerTool({
        ToolId::Arrow,
        "arrow",
        "Arrow",
        "",
        ToolCategory::Drawing,
        true, true, false, true, false, false,  // color, width, text, arrow, shape, fill
        false, QColor()
    });

    registerTool({
        ToolId::Polyline,
        "polyline",
        "Polyline",
        "",
        ToolCategory::Drawing,
        true, true, false, true, false, false,  // color, width, text, arrow, shape, fill
        false, QColor()
    });

    registerTool({
        ToolId::Pencil,
        "pencil",
        "Pencil",
        "",
        ToolCategory::Drawing,
        true, true, false, false, false, false,
        false, QColor()
    });

    registerTool({
        ToolId::Marker,
        "marker",
        "Marker",
        "",
        ToolCategory::Drawing,
        true, true, false, false, false, false,
        false, QColor()
    });

    registerTool({
        ToolId::Shape,
        "shape",
        "Shape",
        "",
        ToolCategory::Drawing,
        true, true, false, false, true, true,  // color, width, shape, fill
        false, QColor()
    });

    registerTool({
        ToolId::Text,
        "text",
        "Text",
        "",
        ToolCategory::Drawing,
        true, false, true, false, false, false,  // color, text formatting
        false, QColor()
    });

    registerTool({
        ToolId::Mosaic,
        "mosaic",
        "Mosaic",
        "",
        ToolCategory::Drawing,
        false, true, false, false, false, false,  // width only
        false, QColor()
    });

    registerTool({
        ToolId::StepBadge,
        "step",
        "Step Badge",
        "",
        ToolCategory::Drawing,
        true, false, false, false, false, false,  // color only
        false, QColor()
    });

    registerTool({
        ToolId::Eraser,
        "eraser",
        "Eraser",
        "",
        ToolCategory::Drawing,
        false, false, false, false, false, false,  // no color/width
        false, QColor()
    });

    // Toggle tools
    registerTool({
        ToolId::LaserPointer,
        "laser",
        "Laser Pointer",
        "",
        ToolCategory::Toggle,
        false, false, false, false, false, false,
        false, QColor()
    });

    registerTool({
        ToolId::CursorHighlight,
        "cursor_highlight",
        "Cursor Highlight",
        "",
        ToolCategory::Toggle,
        false, false, false, false, false, false,
        false, QColor()
    });

    // Action tools
    registerTool({
        ToolId::Undo,
        "undo",
        "Undo",
        "Ctrl+Z",
        ToolCategory::Action,
        false, false, false, false, false, false,
        true, QColor()  // separator before
    });

    registerTool({
        ToolId::Redo,
        "redo",
        "Redo",
        "Ctrl+Y",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, QColor()
    });

    registerTool({
        ToolId::Clear,
        "clear",
        "Clear All",
        "",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, QColor()
    });

    registerTool({
        ToolId::Cancel,
        "cancel",
        "Cancel",
        "Esc",
        ToolCategory::Action,
        false, false, false, false, false, false,
        true, QColor(255, 100, 100)  // red icon
    });

    registerTool({
        ToolId::OCR,
        "ocr",
        "OCR Text Recognition",
        "",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, QColor()
    });

    registerTool({
        ToolId::AutoBlur,
        "auto-blur",
        "Auto Blur (Detect Faces/Text)",
        "",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, QColor()
    });

    registerTool({
        ToolId::Pin,
        "pin",
        "Pin to Desktop",
        "",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, QColor()
    });

    registerTool({
        ToolId::Record,
        "record",
        "Screen Recording",
        "",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, QColor(255, 80, 80)  // red icon
    });

    registerTool({
        ToolId::Save,
        "save",
        "Save",
        "Ctrl+S",
        ToolCategory::Action,
        false, false, false, false, false, false,
        true, QColor()
    });

    registerTool({
        ToolId::Copy,
        "copy",
        "Copy to Clipboard",
        "Ctrl+C",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, QColor()
    });

    registerTool({
        ToolId::Exit,
        "exit",
        "Exit",
        "",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, QColor()
    });
}

void ToolRegistry::registerTool(const ToolDefinition& def) {
    m_definitions.insert(def.id, def);
}

const ToolDefinition& ToolRegistry::get(ToolId id) const {
    auto it = m_definitions.find(id);
    if (it != m_definitions.end()) {
        return it.value();
    }
    return m_defaultDefinition;
}

QVector<ToolId> ToolRegistry::getToolsForToolbar(ToolbarType type) const {
    QVector<ToolId> tools;

    switch (type) {
    case ToolbarType::RegionSelector:
        // Full toolbar for screenshot mode
        // Note: Polyline is accessed via Arrow tool's polyline mode toggle
        tools = {
            ToolId::Selection,
            ToolId::Arrow,
            ToolId::Pencil,
            ToolId::Marker,
            ToolId::Shape,
            ToolId::Text,
            ToolId::Mosaic,
            ToolId::StepBadge,
            ToolId::Eraser,
            ToolId::Undo,
            ToolId::Redo,
            ToolId::Cancel,
            ToolId::OCR,
            ToolId::AutoBlur,
            ToolId::Pin,
            ToolId::Record,
            ToolId::Save,
            ToolId::Copy
        };
        break;

    case ToolbarType::ScreenCanvas:
        // Simplified toolbar for canvas mode
        // Note: Polyline is accessed via Arrow tool's polyline mode toggle
        tools = {
            ToolId::Pencil,
            ToolId::Marker,
            ToolId::Arrow,
            ToolId::Shape,
            ToolId::LaserPointer,
            ToolId::CursorHighlight,
            ToolId::Undo,
            ToolId::Redo,
            ToolId::Clear,
            ToolId::Exit
        };
        break;
    }

    return tools;
}

bool ToolRegistry::isDrawingTool(ToolId id) const {
    return get(id).category == ToolCategory::Drawing;
}

bool ToolRegistry::isActionTool(ToolId id) const {
    return get(id).category == ToolCategory::Action;
}

bool ToolRegistry::isToggleTool(ToolId id) const {
    return get(id).category == ToolCategory::Toggle;
}

bool ToolRegistry::isAnnotationTool(ToolId id) const {
    const auto& def = get(id);
    // Annotation tools are drawing tools that create persistent annotations
    // (excludes toggle tools like laser pointer)
    return def.category == ToolCategory::Drawing;
}

QString ToolRegistry::getIconKey(ToolId id) const {
    return get(id).iconKey;
}

QString ToolRegistry::getTooltip(ToolId id) const {
    return get(id).tooltip;
}
