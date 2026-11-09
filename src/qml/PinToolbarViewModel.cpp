#include "qml/PinToolbarViewModel.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolTraits.h"

#include <QCoreApplication>

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
        // Group boundaries add their own separator; keep the sub-blocks inside
        // Assist (Beautify/Crop/Measure | Undo/Redo | OCR/QR).
        options.separatorBefore = (toolId == ToolId::Undo || toolId == ToolId::OCR);
        options.isAction = (toolId == ToolId::Save
                            || toolId == ToolId::Copy);
        const QString tooltipOverride = (toolId == ToolId::Beautify)
            ? QCoreApplication::translate("PinWindow", "Beautify")
            : QString();
        buttons.append(buildToolButtonEntry(toolId, options, tooltipOverride));
    }

    // "Done" — local toolbar-only action
    ToolButtonOptions doneOptions;
    doneOptions.group = ToolbarGroup::Finish;
    doneOptions.collapsePriority = kNeverCollapse;
    doneOptions.isAction = true;
    buttons.append(buildCustomButtonEntry(ButtonDone,
                                          QStringLiteral("done"),
                                          QStringLiteral("qrc:/icons/icons/done.svg"),
                                          QCoreApplication::translate("PinWindow", "Done (Space/Esc)"),
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
    case static_cast<int>(ToolId::Beautify): emit beautifyClicked(); break;
    case static_cast<int>(ToolId::Copy):    emit copyClicked(); break;
    case static_cast<int>(ToolId::Save):    emit saveClicked(); break;
    case ButtonDone:                        emit doneClicked(); break;
    default: break;
    }
}
