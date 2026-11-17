#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"
#include "settings/AnnotationSettingsManager.h"
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QScopedValueRollback>
#include <cmath>
#include <algorithm>

namespace {
constexpr qreal kBindingHitTolerance = 8.0; // logical screen pixels, independent of DPR

struct TargetGeometry {
    QRectF rect;
    QTransform transform;
    bool ellipse = false;
    QPainterPath path() const {
        QPainterPath path;
        if (ellipse) path.addEllipse(rect); else path.addRect(rect);
        return transform.map(path);
    }
};
std::optional<TargetGeometry> targetGeometry(const AnnotationItem* item)
{
    TargetGeometry result;
    QPointF center;
    if (const auto* shape = dynamic_cast<const ShapeAnnotation*>(item)) {
        result.rect = shape->rectF().normalized();
        center = shape->center();
        result.transform.translate(center.x(), center.y());
        result.transform.rotate(shape->rotation());
        result.transform.scale(shape->scaleX(), shape->scaleY());
        result.transform.translate(-center.x(), -center.y());
        result.ellipse = shape->shapeType() == ShapeType::Ellipse;
    } else if (const auto* text = dynamic_cast<const TextBoxAnnotation*>(item)) {
        result.rect = QRectF(text->position(), text->box().size());
        center = text->center();
        result.transform.translate(center.x(), center.y());
        result.transform.rotate(text->rotation());
        result.transform.scale(text->scale() * (text->mirrorX() ? -1 : 1),
                               text->scale() * (text->mirrorY() ? -1 : 1));
        result.transform.translate(-center.x(), -center.y());
    } else return std::nullopt;
    if (!result.rect.isValid() || !result.transform.isInvertible()
        || !std::isfinite(result.rect.x()) || !std::isfinite(result.rect.y())
        || !std::isfinite(result.rect.width()) || !std::isfinite(result.rect.height())
        || !std::isfinite(result.transform.m11()) || !std::isfinite(result.transform.m12()))
        return std::nullopt;
    return result;
}
}

ArrowBinding AnnotationLayer::bindingAt(const QPointF& pos, qreal viewScale) const
{
    if (!std::isfinite(viewScale) || viewScale <= 0) return {};
    for (auto it = m_items.crbegin(); it != m_items.crend(); ++it) {
        if (!it->item->isVisible()) continue;
        const auto geometry = targetGeometry(it->get());
        if (!geometry) continue;
        const auto path = geometry->path();
        QPainterPathStroker stroker;
        stroker.setWidth(2 * kBindingHitTolerance / viewScale);
        if (!path.contains(pos) && !stroker.createStroke(path).contains(pos)) continue;
        const QPointF local = geometry->transform.inverted().map(pos);
        QPointF anchor(qBound(0.0, (local.x() - geometry->rect.left()) / geometry->rect.width(), 1.0),
                       qBound(0.0, (local.y() - geometry->rect.top()) / geometry->rect.height(), 1.0));
        if (geometry->ellipse) {
            QPointF radius = (anchor - QPointF(0.5, 0.5)) * 2;
            const qreal length = std::hypot(radius.x(), radius.y());
            if (length > 1) anchor = QPointF(0.5, 0.5) + radius / (2 * length);
        }
        return {it->item->persistentId(), anchor};
    }
    return {};
}

void AnnotationLayer::editArrowEndpoint(ArrowAnnotation& arrow, bool end,
                                        const QPoint& pos, qreal viewScale)
{
    const ArrowBinding binding = AnnotationSettingsManager::instance().loadArrowBinding()
        ? bindingAt(pos, viewScale) : ArrowBinding{};
    arrow.setBinding(end, binding);
    if (end) arrow.setEnd(pos); else arrow.setStart(pos);
    resolveArrow(arrow);
    m_bindingHint = binding.targetId;
}

void AnnotationLayer::clearBindingHint() { m_bindingHint = {}; }

void AnnotationLayer::drawBindingHint(QPainter& painter) const
{
    const auto target = m_bindingItems.value(m_bindingHint, nullptr);
    if (!target) return;
    const auto geometry = targetGeometry(target);
    if (!geometry) return;
    painter.save();
    QPen pen(QColor(255, 80, 170), 2.0, Qt::DashLine);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(geometry->path());
    painter.restore();
}

QRect AnnotationLayer::bindingHintBounds() const
{
    auto* target = m_bindingItems.value(m_bindingHint, nullptr);
    return target ? target->boundingRect().adjusted(-4, -4, 4, 4) : QRect();
}

void AnnotationLayer::resolveArrow(ArrowAnnotation& arrow)
{
    for (bool end : {false, true}) {
        const auto binding = arrow.binding(end);
        if (binding.targetId.isNull()) continue;
        const auto geometry = targetGeometry(m_bindingItems.value(binding.targetId, nullptr));
        if (!geometry) continue;
        const QPointF point = geometry->rect.topLeft()
            + QPointF(binding.anchor.x() * geometry->rect.width(),
                      binding.anchor.y() * geometry->rect.height());
        const QPoint resolved = geometry->transform.map(point).toPoint();
        if (end) arrow.setEnd(resolved); else arrow.setStart(resolved);
    }
}

void AnnotationLayer::indexArrow(ArrowAnnotation& arrow)
{
    for (const auto& target : m_arrowTargets.take(&arrow)) {
        auto it = m_bindingDependents.find(target);
        if (it != m_bindingDependents.end()) {
            it->remove(&arrow);
            if (it->isEmpty()) m_bindingDependents.erase(it);
        }
    }
    for (bool end : {false, true}) {
        const auto id = arrow.binding(end).targetId;
        if (!id.isNull()) {
            m_bindingDependents[id].insert(&arrow);
            m_arrowTargets[&arrow].insert(id);
        }
    }
}

void AnnotationLayer::bindingGeometryChanged(AnnotationItem& item)
{
    if (m_updatingBindings) return;
    QScopedValueRollback<bool> guard(m_updatingBindings, true);
    if (auto* arrow = dynamic_cast<ArrowAnnotation*>(&item)) {
        indexArrow(*arrow);
        return;
    }
    // A gesture cache excludes the moving target and its dependent arrows.
    // Retain it throughout the drag; other cached views must not retain stale endpoints.
    if (!m_bindingDependents.contains(item.persistentId())) return;
    for (auto it = m_annotationCaches.begin(); it != m_annotationCaches.end();) {
        const int excluded = it->first.excludeIndex;
        if (excluded < 0 || excluded >= static_cast<int>(m_items.size()) || m_items[excluded].get() != &item)
            it = eraseAnnotationCache(it);
        else ++it;
    }
    // No layer scan on target edits. Only arrows in the reverse dependency index.
    for (auto* arrow : m_bindingDependents.value(item.persistentId())) {
        const QRect before = arrow->boundingRect();
        resolveArrow(*arrow);
        markDirtyRect(before.united(arrow->boundingRect()));
    }
}

void AnnotationLayer::registerBindingItem(AnnotationItem& item, std::size_t order)
{
    m_bindingItems.insert(item.persistentId(), &item);
    m_bindingOrder.insert(&item, order);
    item.setGeometryObserver([this, ptr = &item] { bindingGeometryChanged(*ptr); });
    if (auto* arrow = dynamic_cast<ArrowAnnotation*>(&item)) {
        for (bool end : {false, true}) {
            if (!arrow->binding(end).targetId.isNull()
                && !targetGeometry(m_bindingItems.value(arrow->binding(end).targetId, nullptr)))
                arrow->setBinding(end, {});
        }
        indexArrow(*arrow);
        resolveArrow(*arrow);
    }
}

void AnnotationLayer::rebuildBindingIndex()
{
    QScopedValueRollback<bool> guard(m_updatingBindings, true);
    m_bindingItems.clear();
    m_bindingOrder.clear();
    m_bindingDependents.clear();
    m_arrowTargets.clear();
    for (auto& scene : m_items) {
        if (m_bindingItems.contains(scene->persistentId())) scene->setPersistentId(QUuid::createUuid());
        m_bindingOrder.insert(scene.get(), m_bindingOrder.size());
        m_bindingItems.insert(scene->persistentId(), scene.get());
        auto* item = scene.get();
        item->setGeometryObserver([this, item] { bindingGeometryChanged(*item); });
    }
    for (auto& scene : m_items) {
        if (auto* arrow = dynamic_cast<ArrowAnnotation*>(scene.get())) {
            for (bool end : {false, true}) {
                if (!arrow->binding(end).targetId.isNull()
                    && !targetGeometry(m_bindingItems.value(arrow->binding(end).targetId, nullptr)))
                    arrow->setBinding(end, {});
            }
            indexArrow(*arrow);
            resolveArrow(*arrow);
        }
    }
}

void AnnotationLayer::prepareBindingRemoval(RemovedItem& record, AnnotationItem& item)
{
    // Removed records own just the lost relationships, not unrelated arrow snapshots.
    record.bindings.clear();
    const auto dependents = m_bindingDependents.value(item.persistentId());
    for (auto* arrow : dependents) {
        for (bool end : {false, true}) {
            if (arrow->binding(end).targetId != item.persistentId()) continue;
            record.bindings.push_back({arrow->persistentId(), end, arrow->binding(end)});
            arrow->setBinding(end, {});
        }
    }
    if (auto* arrow = dynamic_cast<ArrowAnnotation*>(&item)) {
        // Remove this arrow from the reverse index without changing its saved bindings.
        const auto savedStart = arrow->binding(false), savedEnd = arrow->binding(true);
        arrow->setBinding(false, {});
        arrow->setBinding(true, {});
        item.setGeometryObserver({});
        arrow->setBinding(false, savedStart);
        arrow->setBinding(true, savedEnd);
    }
    item.setGeometryObserver({});
    m_bindingItems.remove(item.persistentId());
}

void AnnotationLayer::restoreBindings(const std::vector<RemovedItem>& records)
{
    rebuildBindingIndex();
    for (const auto& record : records) {
        for (const auto& saved : record.bindings) {
            auto* arrow = dynamic_cast<ArrowAnnotation*>(m_bindingItems.value(saved.arrowId, nullptr));
            if (arrow && m_bindingItems.contains(saved.binding.targetId)) {
                arrow->setBinding(saved.end, saved.binding);
                resolveArrow(*arrow);
            }
        }
    }
}


std::vector<AnnotationItem*> AnnotationLayer::bindingDragItems(int excludeIndex) const
{
    if (excludeIndex < 0 || excludeIndex >= static_cast<int>(m_items.size())) return {};
    auto* target = m_items[excludeIndex].get();
    std::vector<AnnotationItem*> result{target};
    for (auto* arrow : m_bindingDependents.value(target->persistentId())) result.push_back(arrow);
    std::sort(result.begin(), result.end(), [this](auto* a, auto* b) {
        return m_bindingOrder.value(a) < m_bindingOrder.value(b);
    });
    return result;
}
