#include "tools/ToolRegistry.h"

#include <QCoreApplication>

ToolRegistry& ToolRegistry::instance() {
    static ToolRegistry registry;
    return registry;
}

ToolRegistry::ToolRegistry() {
    // Initialize default definition for unknown tools
    m_defaultDefinition.id = ToolId::Selection;
    m_defaultDefinition.iconKey = "selection";
    m_defaultDefinition.tooltip = QCoreApplication::translate("ToolRegistry", "Unknown Tool");
    m_defaultDefinition.category = ToolCategory::Selection;

    registerTools();
}

void ToolRegistry::registerTools() {
    // Selection tool
    registerTool({
        ToolId::Selection,
        "selection",
        QCoreApplication::translate("ToolRegistry", "Selection"),
        "",
        ToolCategory::Selection,
        false, false, false, false, false, false,  // capabilities
        false, false, false,  // UI visibility: colorPalette, widthControl, colorWidthWidget
        false, QColor()  // UI config
    });

    // Drawing tools
    registerTool({
        ToolId::Arrow,
        "arrow",
        QCoreApplication::translate("ToolRegistry", "Arrow"),
        "",
        ToolCategory::Drawing,
        true, true, false, true, false, false,  // color, width, text, arrow, shape, fill
        true, true, true,  // UI visibility
        false, QColor()
    });

    registerTool({
        ToolId::Polyline,
        "polyline",
        QCoreApplication::translate("ToolRegistry", "Polyline"),
        "",
        ToolCategory::Drawing,
        true, true, false, true, false, false,  // color, width, text, arrow, shape, fill
        true, true, true,  // UI visibility
        false, QColor()
    });

    registerTool({
        ToolId::Pencil,
        "pencil",
        QCoreApplication::translate("ToolRegistry", "Pencil"),
        "",
        ToolCategory::Drawing,
        true, true, false, false, false, false,
        true, true, true,  // UI visibility
        false, QColor()
    });

    registerTool({
        ToolId::Marker,
        "marker",
        QCoreApplication::translate("ToolRegistry", "Marker"),
        "",
        ToolCategory::Drawing,
        true, true, false, false, false, false,
        true, false, true,  // UI visibility: color yes, width no
        false, QColor()
    });

    registerTool({
        ToolId::Shape,
        "shape",
        QCoreApplication::translate("ToolRegistry", "Shape"),
        "",
        ToolCategory::Drawing,
        true, true, false, false, true, true,  // color, width, shape, fill
        true, true, true,  // UI visibility
        false, QColor()
    });

    registerTool({
        ToolId::Text,
        "text",
        QCoreApplication::translate("ToolRegistry", "Text"),
        "",
        ToolCategory::Drawing,
        true, false, true, false, false, false,  // color, text formatting
        true, false, true,  // UI visibility: color yes, width no
        false, QColor()
    });

    registerTool({
        ToolId::Mosaic,
        "mosaic",
        QCoreApplication::translate("ToolRegistry", "Mosaic"),
        "",
        ToolCategory::Drawing,
        false, true, false, false, false, false,  // width only
        false, true, true,  // UI visibility: color no, width yes
        false, QColor()
    });

    registerTool({
        ToolId::Eraser,
        "eraser",
        QCoreApplication::translate("ToolRegistry", "Eraser"),
        "",
        ToolCategory::Drawing,
        false, false, false, false, false, false,  // no color/width UI
        false, false, false,  // UI visibility: none
        false, QColor()
    });

    registerTool({
        ToolId::StepBadge,
        "step-badge",
        QCoreApplication::translate("ToolRegistry", "Step Badge"),
        "",
        ToolCategory::Drawing,
        true, false, false, false, false, false,  // color only
        true, false, true,  // UI visibility: color yes, width no
        false, QColor()
    });

    registerTool({
        ToolId::EmojiSticker,
        "emoji",
        QCoreApplication::translate("ToolRegistry", "Emoji Sticker"),
        "",
        ToolCategory::Drawing,
        false, false, false, false, false, false,  // no color/width UI
        false, false, false,  // UI visibility: none
        false, QColor()
    });

    registerTool({
        ToolId::Crop,
        "crop",
        QCoreApplication::translate("ToolRegistry", "Crop"),
        "",
        ToolCategory::Drawing,
        false, false, false, false, false, false,  // no capabilities
        false, false, false,  // UI visibility: none
        true, QColor()  // separator before, default color
    });

    registerTool({
        ToolId::Measure,
        "measure",
        QCoreApplication::translate("ToolRegistry", "Measure"),
        "",
        ToolCategory::Drawing,
        false, false, false, false, false, false,  // no capabilities
        false, false, false,  // UI visibility: none
        false, QColor()
    });

    // Toggle tools
    registerTool({
        ToolId::CanvasWhiteboard,
        "whiteboard",
        QCoreApplication::translate("ToolRegistry", "Whiteboard"),
        "W",
        ToolCategory::Toggle,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        true, QColor()
    });

    registerTool({
        ToolId::CanvasBlackboard,
        "blackboard",
        QCoreApplication::translate("ToolRegistry", "Blackboard"),
        "B",
        ToolCategory::Toggle,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        false, QColor()
    });

    // Action tools
#ifdef Q_OS_MACOS
    const QString undoShortcut = "Cmd+Z";
    const QString redoShortcut = "Cmd+Shift+Z";
#else
    const QString undoShortcut = "Ctrl+Z";
    const QString redoShortcut = "Ctrl+Y";
#endif

    registerTool({
        ToolId::Undo,
        "undo",
        QCoreApplication::translate("ToolRegistry", "Undo"),
        undoShortcut,
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        true, QColor()  // separator before
    });

    registerTool({
        ToolId::Redo,
        "redo",
        QCoreApplication::translate("ToolRegistry", "Redo"),
        redoShortcut,
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        false, QColor()
    });

    registerTool({
        ToolId::Clear,
        "cancel",
        QCoreApplication::translate("ToolRegistry", "Clear All"),
        "",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        false, QColor()
    });

    registerTool({
        ToolId::Cancel,
        "cancel",
        QCoreApplication::translate("ToolRegistry", "Cancel"),
        "Esc",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        true, QColor(255, 100, 100)  // red icon
    });

    registerTool({
        ToolId::OCR,
        "ocr",
        QCoreApplication::translate("ToolRegistry", "Recognize Text"),
        "",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        false, QColor()
    });

    registerTool({
        ToolId::QRCode,
        "qrcode",
        QCoreApplication::translate("ToolRegistry", "QR Code Scan"),
        "",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        false, QColor()
    });

    registerTool({
        ToolId::Pin,
        "pin",
        QCoreApplication::translate("ToolRegistry", "Pin to Desktop"),
        "",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        false, QColor()
    });

    registerTool({
        ToolId::Record,
        "record",
        QCoreApplication::translate("ToolRegistry", "Screen Recording"),
        "",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        false, QColor(255, 80, 80)  // red icon
    });

#ifdef Q_OS_MACOS
    registerTool({
        ToolId::Save,
        "save",
        QCoreApplication::translate("ToolRegistry", "Save"),
        "Cmd+S",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        false, QColor()
    });

    registerTool({
        ToolId::Copy,
        "copy",
        QCoreApplication::translate("ToolRegistry", "Copy to Clipboard"),
        "Cmd+C",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        false, QColor()
    });
#else
    registerTool({
        ToolId::Save,
        "save",
        QCoreApplication::translate("ToolRegistry", "Save"),
        "Ctrl+S",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        false, QColor()
    });

    registerTool({
        ToolId::Copy,
        "copy",
        QCoreApplication::translate("ToolRegistry", "Copy to Clipboard"),
        "Ctrl+C",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        false, QColor()
    });
#endif

    registerTool({
        ToolId::Exit,
        "cancel",
        QCoreApplication::translate("ToolRegistry", "Exit"),
        "",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        false, QColor()
    });

    // Region-specific tools
    registerTool({
        ToolId::MultiRegion,
        "multi-region",
        QCoreApplication::translate("ToolRegistry", "Multi-Region Capture"),
        "",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
        false, QColor()
    });

    registerTool({
        ToolId::MultiRegionDone,
        "done",
        QCoreApplication::translate("ToolRegistry", "Complete Multi-Region"),
        "",
        ToolCategory::Action,
        false, false, false, false, false, false,
        false, false, false,  // UI visibility: none
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
            ToolId::Shape,
            ToolId::Arrow,
            ToolId::Pencil,
            ToolId::Marker,
            ToolId::Text,
            ToolId::Mosaic,
            ToolId::Eraser,
            ToolId::StepBadge,
            ToolId::EmojiSticker,
            ToolId::Undo,
            ToolId::Redo,
            ToolId::Cancel,
            ToolId::OCR,
            ToolId::QRCode,
            ToolId::Record,
            ToolId::MultiRegion,
            ToolId::Pin,
            ToolId::Save,
            ToolId::Copy
        };
        break;

    case ToolbarType::ScreenCanvas:
        // Simplified toolbar for canvas mode
        // Note: Polyline is accessed via Arrow tool's polyline mode toggle
        tools = {
            ToolId::Shape,
            ToolId::Arrow,
            ToolId::Pencil,
            ToolId::Marker,
            ToolId::Text,
            ToolId::Eraser,
            ToolId::StepBadge,
            ToolId::EmojiSticker,
            ToolId::CanvasWhiteboard,
            ToolId::CanvasBlackboard,
            ToolId::Undo,
            ToolId::Redo,
            ToolId::Clear,
            ToolId::Exit
        };
        break;

    case ToolbarType::PinWindow:
        // Pin window annotation toolbar (done/close is a local toolbar action).
        tools = {
            ToolId::Shape,
            ToolId::Arrow,
            ToolId::Pencil,
            ToolId::Marker,
            ToolId::Text,
            ToolId::Mosaic,
            ToolId::Eraser,
            ToolId::StepBadge,
            ToolId::EmojiSticker,
            ToolId::Crop,
            ToolId::Measure,
            ToolId::Undo,
            ToolId::Redo,
            ToolId::OCR,
            ToolId::QRCode,
            ToolId::Save,
            ToolId::Copy
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
    // Annotation tools are drawing tools that create persistent annotations.
    return def.category == ToolCategory::Drawing;
}

QString ToolRegistry::getIconKey(ToolId id) const {
    return get(id).iconKey;
}

QString ToolRegistry::getTooltip(ToolId id) const {
    return get(id).tooltip;
}

QString ToolRegistry::getTooltipWithShortcut(ToolId id) const {
    const auto& def = get(id);
    if (def.shortcut.isEmpty()) {
        return def.tooltip;
    }
    return QString("%1 (%2)").arg(def.tooltip, def.shortcut);
}

bool ToolRegistry::showColorPalette(ToolId id) const {
    return get(id).showColorPalette;
}

bool ToolRegistry::showWidthControl(ToolId id) const {
    return get(id).showWidthControl;
}

bool ToolRegistry::showColorWidthWidget(ToolId id) const {
    return get(id).showColorWidthWidget;
}
