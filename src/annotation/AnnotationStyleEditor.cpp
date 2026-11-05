#include "annotation/AnnotationStyleEditor.h"

#include "annotations/AnnotationStyle.h"
#include "qml/PinToolOptionsViewModel.h"

AnnotationStyleEditor::AnnotationStyleEditor(AnnotationLayer* layer,
                                             PinToolOptionsViewModel* viewModel,
                                             QObject* parent)
    : QObject(parent)
    , m_layer(layer)
    , m_viewModel(viewModel)
{
    m_idle.setSingleShot(true);
    m_idle.setInterval(kCoalesceIdleMs);
    connect(&m_idle, &QTimer::timeout, this, [this] { m_layer->endModifyCoalescing(); });
    connect(m_layer, &AnnotationLayer::selectionChanged, this, &AnnotationStyleEditor::refreshTarget);
    // Undo/redo and other edits keep the strip in sync with the target.
    connect(m_layer, &AnnotationLayer::changed, this, &AnnotationStyleEditor::syncStripFromTarget);
    connect(m_viewModel, &PinToolOptionsViewModel::selectionStyleEdited,
            this, &AnnotationStyleEditor::onStyleEdited);
}

void AnnotationStyleEditor::setInlineEditingPredicate(std::function<bool()> isInlineEditing)
{
    m_isInlineEditing = std::move(isInlineEditing);
}

void AnnotationStyleEditor::refreshTarget()
{
    m_idle.stop();
    m_layer->endModifyCoalescing();
    const auto id = m_layer->selectedId();
    AnnotationItem* item = id ? m_layer->itemAt(m_layer->indexOfId(id)) : nullptr;
    const bool stylable = item && AnnotationStyle::kindOf(*item) != StyleTargetKind::None
        && !(m_isInlineEditing && m_isInlineEditing());
    if (stylable) {
        m_targetId = id;
        m_viewModel->showForSelection(AnnotationStyle::read(*item));
        emit targetShown();
        return;
    }
    if (m_targetId != 0 || m_viewModel->editingSelection()) {
        m_targetId = 0;
        m_viewModel->clearSelectionTarget();
        emit targetCleared();
    }
}

void AnnotationStyleEditor::syncStripFromTarget()
{
    if (m_targetId == 0) return;
    if (m_layer->selectedId() != m_targetId) {
        refreshTarget();
        return;
    }
    if (AnnotationItem* item = m_layer->itemAt(m_layer->indexOfId(m_targetId)))
        m_viewModel->showForSelection(AnnotationStyle::read(*item));
}

void AnnotationStyleEditor::onStyleEdited(int property, const QVariant& value)
{
    if (m_targetId == 0) return;
    const auto prop = static_cast<StyleProperty>(property);
    const QString key = QString::number(property);
    const bool applied = m_layer->modifyItem(m_targetId, key, [prop, &value](AnnotationItem& item) {
        return AnnotationStyle::apply(item, prop, value);
    });
    if (!applied) return;
    m_idle.start();
    emit styleApplied();
}
