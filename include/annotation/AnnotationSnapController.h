#pragma once
#include "annotation/AnnotationSnapEngine.h"
#include <QPointer>
#include <memory>
class AnnotationLayer;
class AnnotationItem;
class QPainter;

// Shared body-translation session. Editors continue to own commit/cancel.
class AnnotationSnapController {
public:
    void setEnabled(bool enabled) { m_enabled = enabled; if (!enabled) end(); }
    void begin(AnnotationLayer* layer, const QPointF& cursor);
    bool move(const QPointF& cursor, bool suspended);
    void end();
    void invalidate() { const bool session = m_session; end(); m_session = session; }
    void setInputModifiers(Qt::KeyboardModifiers modifiers) { m_eventSuspended = modifiers.testFlag(Qt::AltModifier); }
    void setSuspended(bool suspended) {
        if (m_eventSuspended) m_eventSuspended = suspended;
        if (active()) move(m_lastCursor, suspended);
    }
    bool active() const { return m_id != 0; }
    void draw(QPainter& painter);
    QRect guideBounds() const;
    static std::optional<QRectF> layoutBounds(const AnnotationItem* item);
private:
    bool refreshTargets();
    bool m_enabled = false;
    bool m_session = false;
    std::optional<bool> m_eventSuspended;
    QPointer<AnnotationLayer> m_layer;
    AnnotationSnap::Id m_id = 0;
    QRectF m_startBounds;
    std::shared_ptr<const AnnotationItem> m_original;
    QPointF m_startCursor, m_applied, m_lastCursor;
    std::vector<AnnotationSnap::Target> m_targets;
    AnnotationSnap::Engine m_engine;
    AnnotationSnap::Result m_result;
};
