#include "qml/RegionToolbarViewModel.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolTraits.h"
#include "tools/ToolId.h"
#include "PlatformFeatures.h"

RegionToolbarViewModel::RegionToolbarViewModel(QObject* parent)
    : QObject(parent)
{
    buildButtonList();
}

void RegionToolbarViewModel::buildButtonList()
{
    m_buttons.clear();
    auto& registry = ToolRegistry::instance();
    const QVector<ToolId> tools = registry.getToolsForToolbar(ToolbarType::RegionSelector);

    for (ToolId toolId : tools) {
        if (toolId == ToolId::OCR && !PlatformFeatures::instance().isOCRAvailable()) {
            continue;
        }

        const auto& def = registry.get(toolId);
        QVariantMap entry;
        entry["id"] = static_cast<int>(toolId);
        entry["iconKey"] = def.iconKey;
        entry["iconSource"] = QStringLiteral("qrc:/icons/icons/%1.svg").arg(def.iconKey);
        entry["tooltip"] = registry.getTooltipWithShortcut(toolId);
        entry["separatorBefore"] = def.showSeparatorBefore
                                   || toolId == ToolId::OCR;
        entry["isDrawing"] = ToolTraits::isAnnotationTool(toolId);
        entry["isOCR"] = (toolId == ToolId::OCR);
        entry["isUndo"] = (toolId == ToolId::Undo);
        entry["isRedo"] = (toolId == ToolId::Redo);
        entry["isShare"] = (toolId == ToolId::Share);

        // Button styling: action (blue) vs cancel (red)
        const bool isActionButton = (toolId == ToolId::Pin
                                     || toolId == ToolId::Save
                                     || toolId == ToolId::Copy);
        const bool isCancelButton = (toolId == ToolId::Cancel);
        const bool isRecordButton = (toolId == ToolId::Record);
        entry["isAction"] = isActionButton;
        entry["isCancel"] = isCancelButton;
        entry["isRecord"] = isRecordButton;

        m_buttons.append(entry);
    }
}

void RegionToolbarViewModel::buildMultiRegionButtonList()
{
    m_buttons.clear();
    auto& registry = ToolRegistry::instance();

    // Multi-region mode: only Done and Cancel
    const auto& doneDef = registry.get(ToolId::MultiRegionDone);
    QVariantMap done;
    done["id"] = static_cast<int>(ToolId::MultiRegionDone);
    done["iconKey"] = doneDef.iconKey;
    done["iconSource"] = QStringLiteral("qrc:/icons/icons/%1.svg").arg(doneDef.iconKey);
    done["tooltip"] = registry.getTooltipWithShortcut(ToolId::MultiRegionDone);
    done["separatorBefore"] = false;
    done["isDrawing"] = false;
    done["isOCR"] = false;
    done["isUndo"] = false;
    done["isRedo"] = false;
    done["isShare"] = false;
    done["isAction"] = true;
    done["isCancel"] = false;
    done["isRecord"] = false;
    m_buttons.append(done);

    const auto& cancelDef = registry.get(ToolId::Cancel);
    QVariantMap cancel;
    cancel["id"] = static_cast<int>(ToolId::Cancel);
    cancel["iconKey"] = cancelDef.iconKey;
    cancel["iconSource"] = QStringLiteral("qrc:/icons/icons/%1.svg").arg(cancelDef.iconKey);
    cancel["tooltip"] = registry.getTooltipWithShortcut(ToolId::Cancel);
    cancel["separatorBefore"] = false;
    cancel["isDrawing"] = false;
    cancel["isOCR"] = false;
    cancel["isUndo"] = false;
    cancel["isRedo"] = false;
    cancel["isShare"] = false;
    cancel["isAction"] = false;
    cancel["isCancel"] = true;
    cancel["isRecord"] = false;
    m_buttons.append(cancel);
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
    emit buttonsChanged();
}

QVariantList RegionToolbarViewModel::buttons() const
{
    return m_buttons;
}

int RegionToolbarViewModel::activeTool() const
{
    return m_activeTool;
}

void RegionToolbarViewModel::setActiveTool(int toolId)
{
    if (m_activeTool != toolId) {
        m_activeTool = toolId;
        emit activeToolChanged();
    }
}

bool RegionToolbarViewModel::canUndo() const
{
    return m_canUndo;
}

void RegionToolbarViewModel::setCanUndo(bool value)
{
    if (m_canUndo != value) {
        m_canUndo = value;
        emit canUndoChanged();
    }
}

bool RegionToolbarViewModel::canRedo() const
{
    return m_canRedo;
}

void RegionToolbarViewModel::setCanRedo(bool value)
{
    if (m_canRedo != value) {
        m_canRedo = value;
        emit canRedoChanged();
    }
}

bool RegionToolbarViewModel::ocrAvailable() const
{
    return m_ocrAvailable;
}

void RegionToolbarViewModel::setOCRAvailable(bool value)
{
    if (m_ocrAvailable != value) {
        m_ocrAvailable = value;
        emit ocrAvailableChanged();
    }
}

bool RegionToolbarViewModel::shareInProgress() const
{
    return m_shareInProgress;
}

void RegionToolbarViewModel::setShareInProgress(bool value)
{
    if (m_shareInProgress != value) {
        m_shareInProgress = value;
        emit shareInProgressChanged();
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
