#include "qml/PinToolbarViewModel.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolTraits.h"

PinToolbarViewModel::PinToolbarViewModel(QObject* parent)
    : QObject(parent)
{
    buildButtonList();
}

void PinToolbarViewModel::buildButtonList()
{
    m_buttons.clear();
    auto& registry = ToolRegistry::instance();
    const QVector<ToolId> tools = registry.getToolsForToolbar(ToolbarType::PinWindow);

    for (ToolId toolId : tools) {
        const auto& def = registry.get(toolId);
        QVariantMap entry;
        entry["id"] = static_cast<int>(toolId);
        entry["iconKey"] = def.iconKey;
        entry["iconSource"] = QStringLiteral("qrc:/icons/icons/%1.svg").arg(def.iconKey);
        entry["tooltip"] = registry.getTooltipWithShortcut(toolId);
        entry["separatorBefore"] = def.showSeparatorBefore
                                   || toolId == ToolId::OCR;
        entry["isAction"] = false;
        entry["isDrawing"] = ToolTraits::isAnnotationTool(toolId);
        entry["isOCR"] = (toolId == ToolId::OCR);
        entry["isUndo"] = (toolId == ToolId::Undo);
        entry["isRedo"] = (toolId == ToolId::Redo);
        entry["isShare"] = (toolId == ToolId::Share);
        m_buttons.append(entry);
    }

    // "Done" — local toolbar-only action
    QVariantMap done;
    done["id"] = ButtonDone;
    done["iconKey"] = QStringLiteral("done");
    done["iconSource"] = QStringLiteral("qrc:/icons/icons/done.svg");
    done["tooltip"] = QStringLiteral("Done (Space/Esc)");
    done["separatorBefore"] = true;
    done["isAction"] = false;
    done["isDrawing"] = false;
    done["isOCR"] = false;
    done["isUndo"] = false;
    done["isRedo"] = false;
    done["isShare"] = false;
    m_buttons.append(done);
}

QVariantList PinToolbarViewModel::buttons() const
{
    return m_buttons;
}

int PinToolbarViewModel::activeTool() const
{
    return m_activeTool;
}

void PinToolbarViewModel::setActiveTool(int toolId)
{
    if (m_activeTool != toolId) {
        m_activeTool = toolId;
        emit activeToolChanged();
    }
}

bool PinToolbarViewModel::canUndo() const
{
    return m_canUndo;
}

void PinToolbarViewModel::setCanUndo(bool value)
{
    if (m_canUndo != value) {
        m_canUndo = value;
        emit canUndoChanged();
    }
}

bool PinToolbarViewModel::canRedo() const
{
    return m_canRedo;
}

void PinToolbarViewModel::setCanRedo(bool value)
{
    if (m_canRedo != value) {
        m_canRedo = value;
        emit canRedoChanged();
    }
}

bool PinToolbarViewModel::ocrAvailable() const
{
    return m_ocrAvailable;
}

void PinToolbarViewModel::setOCRAvailable(bool value)
{
    if (m_ocrAvailable != value) {
        m_ocrAvailable = value;
        emit ocrAvailableChanged();
    }
}

bool PinToolbarViewModel::shareInProgress() const
{
    return m_shareInProgress;
}

void PinToolbarViewModel::setShareInProgress(bool value)
{
    if (m_shareInProgress != value) {
        m_shareInProgress = value;
        emit shareInProgressChanged();
    }
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
    case static_cast<int>(ToolId::Copy):    emit copyClicked(); break;
    case static_cast<int>(ToolId::Save):    emit saveClicked(); break;
    case ButtonDone:                        emit doneClicked(); break;
    default: break;
    }
}
