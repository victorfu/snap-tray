#include "qml/CanvasToolbarViewModel.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolTraits.h"
#include "tools/ToolId.h"

namespace {
constexpr int kLaserPointerButtonId = 10001;
}

CanvasToolbarViewModel::CanvasToolbarViewModel(QObject* parent)
    : QObject(parent)
{
    buildButtonList();
}

void CanvasToolbarViewModel::buildButtonList()
{
    m_buttons.clear();
    auto& registry = ToolRegistry::instance();
    const QVector<ToolId> tools = registry.getToolsForToolbar(ToolbarType::ScreenCanvas);

    bool laserInserted = false;
    for (ToolId toolId : tools) {
        // Insert Laser Pointer button before CanvasWhiteboard (matching old setupToolbar)
        if (!laserInserted && toolId == ToolId::CanvasWhiteboard) {
            QVariantMap laserEntry;
            laserEntry["id"] = kLaserPointerButtonId;
            laserEntry["iconKey"] = QStringLiteral("laser-pointer");
            laserEntry["iconSource"] = QStringLiteral("qrc:/icons/icons/laser-pointer.svg");
            laserEntry["tooltip"] = tr("Laser Pointer");
            laserEntry["separatorBefore"] = false;
            laserEntry["isDrawing"] = true;
            laserEntry["isOCR"] = false;
            laserEntry["isUndo"] = false;
            laserEntry["isRedo"] = false;
            laserEntry["isShare"] = false;
            laserEntry["isAction"] = false;
            laserEntry["isCancel"] = false;
            laserEntry["isRecord"] = false;
            m_buttons.append(laserEntry);
            laserInserted = true;
        }

        const auto& def = registry.get(toolId);
        QVariantMap entry;
        entry["id"] = static_cast<int>(toolId);
        entry["iconKey"] = def.iconKey;
        entry["iconSource"] = QStringLiteral("qrc:/icons/icons/%1.svg").arg(def.iconKey);
        entry["tooltip"] = registry.getTooltipWithShortcut(toolId);
        entry["separatorBefore"] = def.showSeparatorBefore;
        entry["isDrawing"] = ToolTraits::isAnnotationTool(toolId);
        entry["isOCR"] = false;
        entry["isUndo"] = (toolId == ToolId::Undo);
        entry["isRedo"] = (toolId == ToolId::Redo);
        entry["isShare"] = false;

        const bool isCancelButton = (toolId == ToolId::Exit);
        entry["isAction"] = false;
        entry["isCancel"] = isCancelButton;
        entry["isRecord"] = false;

        m_buttons.append(entry);
    }
}

QVariantList CanvasToolbarViewModel::buttons() const
{
    return m_buttons;
}

int CanvasToolbarViewModel::activeTool() const
{
    return m_activeTool;
}

void CanvasToolbarViewModel::setActiveTool(int toolId)
{
    if (m_activeTool != toolId) {
        m_activeTool = toolId;
        emit activeToolChanged();
    }
}

bool CanvasToolbarViewModel::canUndo() const
{
    return m_canUndo;
}

void CanvasToolbarViewModel::setCanUndo(bool value)
{
    if (m_canUndo != value) {
        m_canUndo = value;
        emit canUndoChanged();
    }
}

bool CanvasToolbarViewModel::canRedo() const
{
    return m_canRedo;
}

void CanvasToolbarViewModel::setCanRedo(bool value)
{
    if (m_canRedo != value) {
        m_canRedo = value;
        emit canRedoChanged();
    }
}

void CanvasToolbarViewModel::handleButtonClicked(int buttonId)
{
    // Laser pointer (custom ID, not in ToolId enum) → route via toolSelected
    if (buttonId == kLaserPointerButtonId) {
        emit toolSelected(buttonId);
        return;
    }

    // Drawing/annotation tools → toolSelected
    if (buttonId >= 0 && buttonId < static_cast<int>(ToolId::Count)) {
        auto id = static_cast<ToolId>(buttonId);
        if (ToolTraits::isAnnotationTool(id)) {
            emit toolSelected(buttonId);
            return;
        }
    }

    // Action dispatch
    switch (buttonId) {
    case static_cast<int>(ToolId::Undo):             emit undoClicked(); break;
    case static_cast<int>(ToolId::Redo):              emit redoClicked(); break;
    case static_cast<int>(ToolId::Clear):             emit clearClicked(); break;
    case static_cast<int>(ToolId::Exit):              emit exitClicked(); break;
    case static_cast<int>(ToolId::CanvasWhiteboard):  emit canvasWhiteboardClicked(); break;
    case static_cast<int>(ToolId::CanvasBlackboard):  emit canvasBlackboardClicked(); break;
    default: break;
    }
}
