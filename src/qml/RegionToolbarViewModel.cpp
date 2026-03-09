#include "qml/RegionToolbarViewModel.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolTraits.h"
#include "tools/ToolId.h"
#include "PlatformFeatures.h"

RegionToolbarViewModel::RegionToolbarViewModel(QObject* parent)
    : ToolbarViewModelBase(parent)
{
    setOCRAvailable(PlatformFeatures::instance().isOCRAvailable());
    buildButtonList();
}

void RegionToolbarViewModel::buildButtonList()
{
    QVariantList buttons;
    auto& registry = ToolRegistry::instance();
    const QVector<ToolId> tools = registry.getToolsForToolbar(ToolbarType::RegionSelector);

    for (ToolId toolId : tools) {
        if (toolId == ToolId::OCR && !PlatformFeatures::instance().isOCRAvailable()) {
            continue;
        }

        auto options = defaultToolButtonOptions(toolId);
        options.separatorBefore = registry.get(toolId).showSeparatorBefore
                                  || toolId == ToolId::OCR;
        options.isAction = (toolId == ToolId::Pin
                            || toolId == ToolId::Save
                            || toolId == ToolId::Copy);
        options.isCancel = (toolId == ToolId::Cancel);
        options.isRecord = (toolId == ToolId::Record);
        buttons.append(buildToolButtonEntry(toolId, options));
    }

    setButtons(std::move(buttons));
}

void RegionToolbarViewModel::buildMultiRegionButtonList()
{
    QVariantList buttons;

    // Multi-region mode: only Done and Cancel
    ToolButtonOptions doneOptions;
    doneOptions.isAction = true;
    buttons.append(buildToolButtonEntry(ToolId::MultiRegionDone, doneOptions));

    ToolButtonOptions cancelOptions;
    cancelOptions.isCancel = true;
    buttons.append(buildToolButtonEntry(ToolId::Cancel, cancelOptions));

    setButtons(std::move(buttons));
}

void RegionToolbarViewModel::setMultiRegionMode(bool enabled)
{
    if (m_multiRegionMode == enabled) {
        return;
    }
    m_multiRegionMode = enabled;
    if (enabled) {
        buildMultiRegionButtonList();
    } else {
        buildButtonList();
    }
}

void RegionToolbarViewModel::handleButtonClicked(int buttonId)
{
    // Drawing/annotation tools → toolSelected
    if (buttonId >= 0 && buttonId < static_cast<int>(ToolId::Count)) {
        auto id = static_cast<ToolId>(buttonId);
        if (ToolTraits::isAnnotationTool(id) || id == ToolId::Selection) {
            emit toolSelected(buttonId);
            return;
        }
    }

    // Action dispatch
    switch (buttonId) {
    case static_cast<int>(ToolId::Undo):            emit undoClicked(); break;
    case static_cast<int>(ToolId::Redo):             emit redoClicked(); break;
    case static_cast<int>(ToolId::Cancel):           emit cancelClicked(); break;
    case static_cast<int>(ToolId::OCR):              emit ocrClicked(); break;
    case static_cast<int>(ToolId::QRCode):           emit qrCodeClicked(); break;
    case static_cast<int>(ToolId::Pin):              emit pinClicked(); break;
    case static_cast<int>(ToolId::Record):           emit recordClicked(); break;
    case static_cast<int>(ToolId::Share):            emit shareClicked(); break;
    case static_cast<int>(ToolId::Save):             emit saveClicked(); break;
    case static_cast<int>(ToolId::Copy):             emit copyClicked(); break;
    case static_cast<int>(ToolId::MultiRegion):      emit multiRegionToggled(); break;
    case static_cast<int>(ToolId::MultiRegionDone):  emit multiRegionDoneClicked(); break;
    default: break;
    }
}
