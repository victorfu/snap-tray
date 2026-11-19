#include "annotation/AnnotationSelectionController.h"

#include <QHash>
#include <QKeyEvent>
#include <QPainter>
#include <QPen>

#include <algorithm>

namespace {

constexpr int kOutlinePadding = 3;
// Repaint slack around the outline and marquee pens (antialiasing).
constexpr int kPenRepaintMargin = 2;
constexpr qreal kOutlineWidth = 1.0;
// The selection accent of TransformationGizmo's handles.
const QColor kOutlineColor(0, 174, 255);
const QColor kMarqueeFill(0, 174, 255, 40);

const QHash<int, QPoint>& arrowSteps()
{
    static const QHash<int, QPoint> steps = {
        {Qt::Key_Left, QPoint(-1, 0)},
        {Qt::Key_Right, QPoint(1, 0)},
        {Qt::Key_Up, QPoint(0, -1)},
        {Qt::Key_Down, QPoint(0, 1)},
    };
    return steps;
}

} // namespace

AnnotationSelectionController::AnnotationSelectionController(AnnotationLayer* layer)
    : m_layer(layer)
{
}

bool AnnotationSelectionController::handlePress(const QPoint& pos,
                                                Qt::KeyboardModifiers modifiers,
                                                bool marqueeAllowed)
{
    if (!m_layer || m_layer->isHistoryLocked()) return false;
    // A release that never came: keep the drag, like a single-object gesture.
    if (isActive()) handleRelease(m_currentPos);
    const int hit = m_layer->hitTestSelectable(pos);
    const auto hitId = m_layer->idAt(hit);

    if (modifiers.testFlag(Qt::ShiftModifier)) {
        if (hitId != 0) {
            m_layer->toggleSelected(hitId);
            return true;
        }
        if (!marqueeAllowed) {
            // A Shift-constrained drawing starts here; a group must not stay
            // selected behind it, as with a plain press elsewhere.
            if (m_layer->isMultiSelection()) m_layer->clearSelection();
            return false;
        }
        m_mode = Mode::Marquee;
        m_pressPos = m_currentPos = pos;
        m_marqueeBase = m_layer->selectedIds();
        return true;
    }

    if (!m_layer->isMultiSelection()) return false;
    if (hitId != 0 && m_layer->isSelected(hitId)) {
        // The drag begins on the first move, so a plain click records nothing.
        m_mode = Mode::Pending;
        m_pressPos = m_currentPos = pos;
        return true;
    }
    // Anything else ends the group and is handled as an ordinary press.
    m_layer->clearSelection();
    return false;
}

bool AnnotationSelectionController::handleMove(const QPoint& pos, Qt::MouseButtons buttons)
{
    if (!m_layer || !isActive()) return false;
    if (!buttons.testFlag(Qt::LeftButton)) return handleRelease(m_currentPos);
    updatePointerPosition(pos);
    return true;
}

void AnnotationSelectionController::updatePointerPosition(const QPoint& pos)
{
    m_currentPos = pos;
    if (m_mode == Mode::Pending) {
        if (pos == m_pressPos) return;
        if (!m_layer->beginGroupGesture()) {
            m_mode = Mode::Idle;
            return;
        }
        m_mode = Mode::Dragging;
    }
    if (m_mode == Mode::Dragging) m_layer->updateGroupGesture(pos - m_pressPos);
}

bool AnnotationSelectionController::handleRelease(const QPoint& pos)
{
    if (!m_layer || !isActive()) return false;
    updatePointerPosition(pos);
    const Mode mode = m_mode;
    const QRect area = marqueeRect();
    m_mode = Mode::Idle;
    if (mode == Mode::Dragging) {
        m_layer->commitGroupGesture();
    } else if (mode == Mode::Marquee) {
        if (area.width() >= kMarqueeMinimumDrag || area.height() >= kMarqueeMinimumDrag) {
            auto ids = m_marqueeBase;
            for (auto id : m_layer->selectableIdsIntersecting(area)) {
                if (std::find(ids.cbegin(), ids.cend(), id) == ids.cend()) ids.push_back(id);
            }
            m_layer->setSelectedIds(ids);
        }
    }
    m_marqueeBase.clear();
    return true;
}

bool AnnotationSelectionController::cancel()
{
    if (!isActive()) return false;
    if (m_mode == Mode::Dragging && m_layer) m_layer->cancelGroupGesture();
    m_mode = Mode::Idle;
    m_marqueeBase.clear();
    return true;
}

bool AnnotationSelectionController::handleKey(
    const QKeyEvent* event, const std::function<QPointF(const QPoint&)>& stepToAnnotation)
{
    if (!m_layer || !event) return false;

    if (event->key() == Qt::Key_Escape) {
        if (cancel()) return true;
        if (!m_layer->isMultiSelection()) return false;
        m_layer->clearSelection();
        return true;
    }
    if (isActive()) {
        return (m_mode == Mode::Pending || m_mode == Mode::Dragging)
            && arrowSteps().contains(event->key());
    }

    if (event->matches(QKeySequence::SelectAll)) {
        // With nothing selectable the key is left to the host.
        m_layer->selectAll();
        return m_layer->selectionCount() > 0;
    }

    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        // A single selection keeps each host's own Delete behaviour.
        if (event->modifiers() & ~(Qt::KeypadModifier) || !m_layer->isMultiSelection()) return false;
        return m_layer->removeSelectedItems();
    }

    const auto step = arrowSteps().constFind(event->key());
    if (step == arrowSteps().constEnd() || !m_layer->isMultiSelection()) return false;
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (modifiers != Qt::NoModifier && modifiers != Qt::ShiftModifier) return false;
    const QPoint screenStep = *step * (modifiers == Qt::ShiftModifier ? kNudgeLargeStep : kNudgeStep);
    const QPointF delta = stepToAnnotation ? stepToAnnotation(screenStep) : QPointF(screenStep);
    if (!m_lastNudge.isValid() || m_lastNudge.elapsed() > kNudgeCoalesceMs) m_layer->endModifyCoalescing();
    m_layer->translateSelectedItems(delta, QStringLiteral("multiSelectionNudge"));
    m_lastNudge.start();
    return true;   // consumed even at a no-op, so it never moves the region
}

QRect AnnotationSelectionController::marqueeRect() const
{
    if (m_mode != Mode::Marquee) return {};
    return QRect(m_pressPos, m_currentPos).normalized();
}

void AnnotationSelectionController::draw(QPainter& painter) const
{
    if (!m_layer) return;
    painter.save();
    QPen pen(kOutlineColor, kOutlineWidth, Qt::DashLine);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (m_layer->isMultiSelection()) {
        for (auto id : m_layer->selectedIds()) {
            const AnnotationItem* item = m_layer->itemAt(m_layer->indexOfId(id));
            if (item) {
                painter.drawRect(item->boundingRect().adjusted(
                    -kOutlinePadding, -kOutlinePadding, kOutlinePadding, kOutlinePadding));
            }
        }
    }
    const QRect marquee = marqueeRect();
    if (!marquee.isEmpty()) {
        pen.setStyle(Qt::SolidLine);
        painter.setPen(pen);
        painter.setBrush(kMarqueeFill);
        painter.drawRect(marquee);
    }
    painter.restore();
}

QRect AnnotationSelectionController::visualRect() const
{
    QRect area;
    if (m_layer && m_layer->isMultiSelection()) {
        const int margin = kOutlinePadding + kPenRepaintMargin;
        area = m_layer->selectionBoundingRect().adjusted(-margin, -margin, margin, margin);
    }
    const QRect marquee = marqueeRect();
    if (!marquee.isEmpty()) area = area.united(marquee.adjusted(-kPenRepaintMargin, -kPenRepaintMargin, kPenRepaintMargin, kPenRepaintMargin));
    return area;
}
