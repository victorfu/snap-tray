#include "qml/CanvasToolbarViewModel.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolTraits.h"
#include "tools/ToolId.h"

namespace {
constexpr int kLaserPointerButtonId = 10001;
}

CanvasToolbarViewModel::CanvasToolbarViewModel(QObject* parent)
    : ToolbarViewModelBase(parent)
{
    setOCRAvailable(false);
    buildButtonList();
}

void CanvasToolbarViewModel::buildButtonList()
{
    QVariantList buttons;
    auto& registry = ToolRegistry::instance();
    const QVector<ToolId> tools = registry.getToolsForToolbar(ToolbarType::ScreenCanvas);

    bool laserInserted = false;
    for (ToolId toolId : tools) {
        // Insert Laser Pointer button before CanvasWhiteboard (matching old setupToolbar)
        if (!laserInserted && toolId == ToolId::CanvasWhiteboard) {
            ToolButtonOptions laserOptions;
            laserOptions.isDrawing = true;
            buttons.append(buildCustomButtonEntry(kLaserPointerButtonId,
                                                  QStringLiteral("laser-pointer"),
                                                  QStringLiteral("qrc:/icons/icons/laser-pointer.svg"),
                                                  tr("Laser Pointer"),
                                                  laserOptions));
            laserInserted = true;
        }

        auto options = defaultToolButtonOptions(toolId);
        options.separatorBefore = registry.get(toolId).showSeparatorBefore;
        options.isCancel = (toolId == ToolId::Exit);
        buttons.append(buildToolButtonEntry(toolId, options));
    }

    setButtons(std::move(buttons));
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
