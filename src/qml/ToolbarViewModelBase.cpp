#include "qml/ToolbarViewModelBase.h"

#include <algorithm>
#include <array>

#include "tools/ToolRegistry.h"
#include "tools/ToolTraits.h"

namespace {

bool isExportActionTool(ToolId toolId)
{
    static constexpr std::array<ToolId, 3> kExportActionTools = {
        ToolId::Pin,
        ToolId::Save,
        ToolId::Copy,
    };
    return std::find(kExportActionTools.begin(), kExportActionTools.end(), toolId) != kExportActionTools.end();
}

}  // namespace

ToolbarViewModelBase::ToolbarViewModelBase(QObject* parent)
    : QObject(parent)
{
}

QVariantList ToolbarViewModelBase::buttons() const
{
    return m_buttons;
}

int ToolbarViewModelBase::activeTool() const
{
    return m_activeTool;
}

void ToolbarViewModelBase::setActiveTool(int toolId)
{
    if (m_activeTool != toolId) {
        m_activeTool = toolId;
        emit activeToolChanged();
    }
}

bool ToolbarViewModelBase::canUndo() const
{
    return m_canUndo;
}

void ToolbarViewModelBase::setCanUndo(bool value)
{
    if (m_canUndo != value) {
        m_canUndo = value;
        emit canUndoChanged();
    }
}

bool ToolbarViewModelBase::canRedo() const
{
    return m_canRedo;
}

void ToolbarViewModelBase::setCanRedo(bool value)
{
    if (m_canRedo != value) {
        m_canRedo = value;
        emit canRedoChanged();
    }
}

bool ToolbarViewModelBase::ocrAvailable() const
{
    return m_ocrAvailable;
}

void ToolbarViewModelBase::setOCRAvailable(bool value)
{
    if (m_ocrAvailable != value) {
        m_ocrAvailable = value;
        emit ocrAvailableChanged();
    }
}

bool ToolbarViewModelBase::autoBlurProcessing() const
{
    return m_autoBlurProcessing;
}

void ToolbarViewModelBase::setAutoBlurProcessing(bool value)
{
    if (m_autoBlurProcessing != value) {
        m_autoBlurProcessing = value;
        emit autoBlurProcessingChanged();
    }
}

ToolbarViewModelBase::ToolButtonOptions ToolbarViewModelBase::defaultToolButtonOptions(ToolId toolId) const
{
    ToolButtonOptions options;
    options.isDrawing = ToolTraits::isAnnotationTool(toolId);
    options.isOCR = (toolId == ToolId::OCR);
    options.isUndo = (toolId == ToolId::Undo);
    options.isRedo = (toolId == ToolId::Redo);
    options.isExportAction = isExportActionTool(toolId);
    return options;
}

QVariantMap ToolbarViewModelBase::buildToolButtonEntry(ToolId toolId,
                                                       const ToolButtonOptions& options,
                                                       QString tooltipOverride) const
{
    auto& registry = ToolRegistry::instance();
    const auto& def = registry.get(toolId);
    ToolButtonOptions resolved = options;
    if (!resolved.group) resolved.group = toolbarGroup(toolId);
    if (!resolved.collapsePriority) resolved.collapsePriority = toolbarCollapsePriority(toolId);
    return buildCustomButtonEntry(static_cast<int>(toolId),
                                  def.iconKey,
                                  QStringLiteral("qrc:/icons/icons/%1.svg").arg(def.iconKey),
                                  tooltipOverride.isNull()
                                      ? registry.getTooltipWithShortcut(toolId)
                                      : tooltipOverride,
                                  resolved);
}

QVariantMap ToolbarViewModelBase::buildCustomButtonEntry(int id,
                                                         const QString& iconKey,
                                                         const QString& iconSource,
                                                         const QString& tooltip,
                                                         const ToolButtonOptions& options) const
{
    QVariantMap entry;
    entry[QStringLiteral("id")] = id;
    entry[QStringLiteral("iconKey")] = iconKey;
    entry[QStringLiteral("iconSource")] = iconSource;
    entry[QStringLiteral("tooltip")] = tooltip;
    entry[QStringLiteral("separatorBefore")] = options.separatorBefore;
    entry[QStringLiteral("isDrawing")] = options.isDrawing;
    entry[QStringLiteral("isOCR")] = options.isOCR;
    entry[QStringLiteral("isUndo")] = options.isUndo;
    entry[QStringLiteral("isRedo")] = options.isRedo;
    entry[QStringLiteral("isAction")] = options.isAction;
    entry[QStringLiteral("isExportAction")] = options.isExportAction;
    entry[QStringLiteral("isCancel")] = options.isCancel;
    entry[QStringLiteral("group")] = static_cast<int>(options.group.value_or(ToolbarGroup::Assist));
    entry[QStringLiteral("collapsePriority")] = options.collapsePriority.value_or(kNeverCollapse);
    return entry;
}

void ToolbarViewModelBase::setButtons(QVariantList buttons)
{
    // A group change starts a separator plus the wider group gap; unranked
    // drawing tools collapse from the end of the row first.
    int previousGroup = -1;
    for (int i = 0; i < buttons.size(); ++i) {
        QVariantMap entry = buttons[i].toMap();
        const int group = entry.value(QStringLiteral("group")).toInt();
        const bool groupStart = previousGroup != -1 && group != previousGroup;
        entry[QStringLiteral("groupStart")] = groupStart;
        if (groupStart) entry[QStringLiteral("separatorBefore")] = true;
        if (entry.value(QStringLiteral("collapsePriority")).toInt()
                == ToolbarTraitsDetail::kDrawBasePriority)
            entry[QStringLiteral("collapsePriority")] = ToolbarTraitsDetail::kDrawBasePriority - i;
        buttons[i] = entry;
        previousGroup = group;
    }
    m_buttons = std::move(buttons);
    emit buttonsChanged();
}
