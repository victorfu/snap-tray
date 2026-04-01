#include "qml/PinToolbarViewModel.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolTraits.h"

PinToolbarViewModel::PinToolbarViewModel(QObject* parent)
    : ToolbarViewModelBase(parent)
{
    buildButtonList();
}

void PinToolbarViewModel::buildButtonList()
{
    QVariantList buttons;
    auto& registry = ToolRegistry::instance();
    const QVector<ToolId> tools = registry.getToolsForToolbar(ToolbarType::PinWindow);

    for (ToolId toolId : tools) {
        auto options = defaultToolButtonOptions(toolId);
        // Keep Beautify/Crop/Measure in one processing section without
        // using the accent-colored action styling.
        options.separatorBefore = (toolId == ToolId::Beautify)
                                  || ((registry.get(toolId).showSeparatorBefore
                                       || toolId == ToolId::OCR)
                                      && toolId != ToolId::Crop);
        options.isAction = (toolId == ToolId::Save
                            || toolId == ToolId::Copy);
        buttons.append(buildToolButtonEntry(toolId, options));
    }

    // "Done" — local toolbar-only action
    ToolButtonOptions doneOptions;
    doneOptions.separatorBefore = true;
    doneOptions.isAction = true;
    buttons.append(buildCustomButtonEntry(ButtonDone,
                                          QStringLiteral("done"),
                                          QStringLiteral("qrc:/icons/icons/done.svg"),
                                          QStringLiteral("Done (Space/Esc)"),
                                          doneOptions));
    setButtons(std::move(buttons));
}

void PinToolbarViewModel::handleButtonClicked(int buttonId)
{
    // Drawing tools → toolSelected
    if (buttonId >= 0 && buttonId < static_cast<int>(ToolId::Count)) {
        auto id = static_cast<ToolId>(buttonId);
        if (ToolTraits::isAnnotationTool(id)) {
            emit toolSelected(buttonId);
            return;
        }
    }

    // Action dispatch
    switch (buttonId) {
    case static_cast<int>(ToolId::Undo):    emit undoClicked(); break;
    case static_cast<int>(ToolId::Redo):    emit redoClicked(); break;
    case static_cast<int>(ToolId::OCR):     emit ocrClicked(); break;
    case static_cast<int>(ToolId::QRCode):  emit qrCodeClicked(); break;
    case static_cast<int>(ToolId::Share):   emit shareClicked(); break;
    case static_cast<int>(ToolId::Beautify): emit beautifyClicked(); break;
    case static_cast<int>(ToolId::Copy):    emit copyClicked(); break;
    case static_cast<int>(ToolId::Save):    emit saveClicked(); break;
    case ButtonDone:                        emit doneClicked(); break;
    default: break;
    }
}
