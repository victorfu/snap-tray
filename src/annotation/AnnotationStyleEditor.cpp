#include "annotation/AnnotationStyleEditor.h"

#include "annotations/AnnotationZOrderCommands.h"
#include "annotations/AnnotationStyle.h"
#include "qml/PinToolOptionsViewModel.h"

#include <QAccessible>

#include <vector>

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
    connect(m_viewModel, &PinToolOptionsViewModel::zOrderRequested, this, [this](int move) {
        applyZOrder(static_cast<AnnotationLayer::ZOrderMove>(move));
    });
}

void AnnotationStyleEditor::setAnnouncementTarget(std::function<QObject*()> target)
{
    m_announcementTarget = std::move(target);
}

bool AnnotationStyleEditor::applyZOrder(AnnotationLayer::ZOrderMove move)
{
    // A pending style edit ends here, and its idle timer must not split
    // repeated moves into several undo steps.
    m_idle.stop();
    if (!m_layer->moveSelectedItem(move)) return false;
    if (QObject* target = m_announcementTarget ? m_announcementTarget() : parent()) {
        QAccessibleAnnouncementEvent event(target, AnnotationZOrderCommands::announcement(move));
        QAccessible::updateAccessibility(&event);
    }
    emit styleApplied();
    return true;
}

bool AnnotationStyleEditor::handleZOrderKey(const QKeyEvent* event)
{
    const auto move = AnnotationZOrderCommands::moveForKeyEvent(event);
    if (!move) return false;
    // A group has no drawing-order move (#370), but the key must not fall
    // through to another action such as a pin's Reset Opacity.
    if (m_layer->isMultiSelection()) return true;
    if (m_layer->selectedIndex() < 0) return false;
    applyZOrder(*move);
    return true;
}

void AnnotationStyleEditor::syncZOrderAvailability()
{
    using Move = AnnotationLayer::ZOrderMove;
    m_viewModel->setZOrderAvailability(m_layer->zOrderTargetIndex(Move::ToFront) >= 0,
                                       m_layer->zOrderTargetIndex(Move::ToBack) >= 0);
}

void AnnotationStyleEditor::setInlineEditingPredicate(std::function<bool()> isInlineEditing)
{
    m_isInlineEditing = std::move(isInlineEditing);
}

void AnnotationStyleEditor::refreshTarget()
{
    m_idle.stop();
    m_layer->endModifyCoalescing();
    if (showMultiTarget()) return;
    const auto id = m_layer->selectedId();
    AnnotationItem* item = id ? m_layer->itemAt(m_layer->indexOfId(id)) : nullptr;
    const bool stylable = item && AnnotationStyle::kindOf(*item) != StyleTargetKind::None
        && !(m_isInlineEditing && m_isInlineEditing());
    syncZOrderAvailability();
    if (stylable) {
        m_targetId = id;
        m_multiTarget = false;   // a group that shrank to one annotation
        m_viewModel->showForSelection(AnnotationStyle::read(*item));
        emit targetShown();
        return;
    }
    if (m_targetId != 0 || m_multiTarget || m_viewModel->editingSelection()) {
        m_targetId = 0;
        m_multiTarget = false;
        m_viewModel->clearSelectionTarget();
        emit targetCleared();
    }
}

bool AnnotationStyleEditor::showMultiTarget()
{
    if (!m_layer->isMultiSelection() || (m_isInlineEditing && m_isInlineEditing())) return false;
    const auto ids = m_layer->selectedIds();
    std::vector<AnnotationStyleValues> targets;
    for (auto id : ids) {
        const AnnotationItem* item = m_layer->itemAt(m_layer->indexOfId(id));
        if (item && AnnotationStyle::kindOf(*item) != StyleTargetKind::None)
            targets.push_back(AnnotationStyle::read(*item));
    }
    // Only emoji selected: nothing to style, so no target.
    if (targets.empty()) return false;
    const bool entering = !m_multiTarget;
    m_targetId = 0;
    m_multiTarget = true;
    m_viewModel->showForMultiSelection(targets, static_cast<int>(ids.size()));
    if (entering) emit targetShown();
    return true;
}

void AnnotationStyleEditor::syncStripFromTarget()
{
    syncZOrderAvailability();
    if (m_multiTarget) {
        // Undo/redo or a group edit may change values or end the group. A
        // still-open coalesced edit (a slider drag) must stay open.
        if (!showMultiTarget()) refreshTarget();
        return;
    }
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
    const auto prop = static_cast<StyleProperty>(property);
    const QString key = QString::number(property);
    if (m_multiTarget) {
        // One undo step for the whole group; annotations without the property
        // are left as they are (#370).
        if (!m_layer->modifySelectedItems(key, [prop, &value](AnnotationItem& item) {
                return AnnotationStyle::apply(item, prop, value);
            })) return;
        m_idle.start();
        emit styleApplied();
        return;
    }
    if (m_targetId == 0) return;
    const bool applied = m_layer->modifyItem(m_targetId, key, [prop, &value](AnnotationItem& item) {
        return AnnotationStyle::apply(item, prop, value);
    });
    if (!applied) return;
    m_idle.start();
    emit styleApplied();
}
