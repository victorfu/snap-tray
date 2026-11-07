#include "annotation/AnnotationSnapController.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/AnnotationGeometry.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "settings/AnnotationSettingsManager.h"
#include <QPainter>
#include <cmath>
#include <algorithm>

std::optional<QRectF> AnnotationSnapController::layoutBounds(const AnnotationItem* item) {
    if (!item || !item->isVisible()) return std::nullopt;
    QRectF bounds;
    if (auto* shape = dynamic_cast<const ShapeAnnotation*>(item)) {
        if (!qFuzzyIsNull(shape->rotation())) return std::nullopt;
        const QRectF design = shape->rectF().normalized();
        const QSizeF extent(design.width() * shape->scaleX(), design.height() * shape->scaleY());
        bounds = QRectF(shape->center() - QPointF(extent.width()/2, extent.height()/2), extent);
    } else if (auto* text = dynamic_cast<const TextBoxAnnotation*>(item)) {
        if (!qFuzzyIsNull(text->rotation())) return std::nullopt;
        const QSizeF extent = text->box().size() * text->scale();
        bounds = QRectF(text->center() - QPointF(extent.width()/2, extent.height()/2), extent);
    } else if (auto* emoji = dynamic_cast<const EmojiStickerAnnotation*>(item)) {
        if (!qFuzzyIsNull(emoji->rotation())) return std::nullopt;
        bounds = emoji->transformedBoundingPolygon().boundingRect();
    } else return std::nullopt;
    if (!bounds.isValid() || !std::isfinite(bounds.left()) || !std::isfinite(bounds.top())
        || !std::isfinite(bounds.right()) || !std::isfinite(bounds.bottom())) return std::nullopt;
    return bounds;
}
void AnnotationSnapController::begin(AnnotationLayer* layer, const QPointF& cursor) {
    end();
    if (!m_enabled || !layer) return;
    const auto bounds = layoutBounds(layer->selectedItem());
    if (!bounds) return;
    m_session = true;
    m_layer = layer;
    m_id = layer->selectedId();
    m_startBounds = *bounds;
    m_original = layer->selectedItem()->clone();
    m_startCursor = m_lastCursor = cursor;
    refreshTargets();
}
bool AnnotationSnapController::refreshTargets() {
    if (!m_layer || m_layer->selectedId() != m_id || !layoutBounds(m_layer->selectedItem())) {
        end(); m_session = true; return false;
    }
    std::vector<AnnotationSnap::Target> targets;
    for (int i = 0; i < static_cast<int>(m_layer->itemCount()); ++i) {
        const auto id = m_layer->idAt(i);
        if (id == m_id) continue;
        if (auto bounds = layoutBounds(m_layer->itemAt(i))) targets.push_back({id, *bounds});
    }
    std::sort(targets.begin(), targets.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    const bool changed = targets.size() != m_targets.size() || !std::equal(targets.begin(), targets.end(), m_targets.begin(),
        [](const auto& a, const auto& b) { return a.id == b.id && a.bounds == b.bounds; });
    if (changed) { m_targets = targets; m_engine.setTargets(targets); m_result = {}; }
    return true;
}
bool AnnotationSnapController::move(const QPointF& cursor, bool suspended) {
    if (!m_session) return false;
    if (!active() || !refreshTargets()) return true;
    m_lastCursor = cursor;
    const QPointF raw = cursor - m_startCursor;
    if (m_eventSuspended.value_or(suspended) || !AnnotationSettingsManager::instance().loadAlignmentAssist() || raw.isNull()) m_result = {};
    else m_result = m_engine.query(m_startBounds.translated(raw), 1.0, m_result.constraints);
    const QPointF total = raw + m_result.correction;
    if (!m_layer->ensureGesture()) { end(); return true; }
    if (total.isNull()) {
        // Restore exactly on a round trip, including fractional geometry. Tiny
        // floating-point subtraction residue must not create an empty Undo.
        AnnotationGeometry::copy(*m_original, *m_layer->selectedItem());
    } else {
        m_layer->selectedItem()->translate(total - m_applied);
    }
    m_applied = total;
    return true;
}
void AnnotationSnapController::end() {
    m_session = false;
    m_eventSuspended.reset();
    m_original.reset();
    m_layer = nullptr; m_id = 0; m_applied = {}; m_targets.clear(); m_engine.setTargets({}); m_result = {};
}
QRect AnnotationSnapController::guideBounds() const {
    QRectF bounds;
    for (const auto& c : m_result.constraints) if (c) for (const auto& l : c->guides)
        bounds = bounds.united(QRectF(l.p1(), l.p2()).normalized().adjusted(-5, -5, 5, 5));
    return bounds.toAlignedRect();
}
void AnnotationSnapController::draw(QPainter& painter) {
    if (!active() || !refreshTargets()) return;
    painter.save();
    QPen pen(QColor(255, 80, 170), 1.0); pen.setCosmetic(true);
    painter.setPen(pen);
    for (const auto& c : m_result.constraints) if (c) for (const auto& l : c->guides) {
        painter.drawLine(l);
        if (c->kind == AnnotationSnap::Kind::Spacing) {
            const QPointF tick = c->axis ? QPointF(4, 0) : QPointF(0, 4);
            painter.drawLine(l.p1()-tick, l.p1()+tick); painter.drawLine(l.p2()-tick, l.p2()+tick);
        }
    }
    painter.restore();
}
