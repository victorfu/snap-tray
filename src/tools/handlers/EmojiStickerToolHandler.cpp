#include "tools/handlers/EmojiStickerToolHandler.h"
#include "tools/ToolContext.h"
#include "TransformationGizmo.h"

#include <QCursor>
#include <QtMath>

namespace {

qreal normalizeAngleDelta(qreal deltaDegrees)
{
    while (deltaDegrees > 180.0) {
        deltaDegrees -= 360.0;
    }
    while (deltaDegrees < -180.0) {
        deltaDegrees += 360.0;
    }
    return deltaDegrees;
}

} // namespace

void EmojiStickerToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    Q_UNUSED(ctx);
    Q_UNUSED(pos);
}

void EmojiStickerToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (!ctx->annotationLayer) {
        return;
    }

    if (m_currentEmoji.isEmpty()) {
        return;
    }

    auto sticker = std::make_unique<EmojiStickerAnnotation>(pos, m_currentEmoji);
    const QRect dirtyBounds = sticker->boundingRect();

    ctx->addItem(std::move(sticker));
    ctx->repaint(dirtyBounds);
}

QCursor EmojiStickerToolHandler::cursor() const {
    return Qt::CrossCursor;
}

bool EmojiStickerToolHandler::handleInteractionPress(ToolContext* ctx,
                                                     const QPoint& pos,
                                                     Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers);

    if (!canHandleInteraction(ctx)) {
        return false;
    }

    auto* layer = ctx->annotationLayer;
    if (auto* emojiItem = selectedEmoji(ctx)) {
        const GizmoHandle handle = TransformationGizmo::hitTest(emojiItem, pos);
        if (handle != GizmoHandle::None) {
            m_activeHandle = handle;
            if (handle == GizmoHandle::Body) {
                m_isDragging = true;
                m_dragStart = pos;
            } else if (handle == GizmoHandle::Rotation) {
                m_isRotating = true;
                m_startCenter = emojiItem->center();
                m_startRotation = emojiItem->rotation();
                const QPointF delta = QPointF(pos) - m_startCenter;
                m_startAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
            } else {
                m_isScaling = true;
                m_startCenter = emojiItem->center();
                m_startScale = emojiItem->scale();
                const QPointF delta = QPointF(pos) - m_startCenter;
                m_startDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
            }
            if (ctx->requestHostFocus) {
                ctx->requestHostFocus();
            }
            return true;
        }
    }

    const int hitIndex = layer->hitTestEmojiSticker(pos);
    if (hitIndex < 0) {
        return false;
    }

    layer->setSelectedIndex(hitIndex);
    if (auto* emojiItem = selectedEmoji(ctx)) {
        const GizmoHandle handle = TransformationGizmo::hitTest(emojiItem, pos);
        m_activeHandle = handle == GizmoHandle::None ? GizmoHandle::Body : handle;
        if (m_activeHandle == GizmoHandle::Body) {
            m_isDragging = true;
            m_dragStart = pos;
        } else if (m_activeHandle == GizmoHandle::Rotation) {
            m_isRotating = true;
            m_startCenter = emojiItem->center();
            m_startRotation = emojiItem->rotation();
            const QPointF delta = QPointF(pos) - m_startCenter;
            m_startAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
        } else {
            m_isScaling = true;
            m_startCenter = emojiItem->center();
            m_startScale = emojiItem->scale();
            const QPointF delta = QPointF(pos) - m_startCenter;
            m_startDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
        }
    }

    if (ctx->requestHostFocus) {
        ctx->requestHostFocus();
    }
    return true;
}

bool EmojiStickerToolHandler::handleInteractionMove(ToolContext* ctx,
                                                    const QPoint& pos,
                                                    Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers);

    auto* emojiItem = selectedEmoji(ctx);
    if (!emojiItem) {
        return false;
    }

    if (m_isDragging) {
        const QPoint delta = pos - m_dragStart;
        emojiItem->moveBy(delta);
        m_dragStart = pos;
    } else if (m_isRotating) {
        const QPointF delta = QPointF(pos) - m_startCenter;
        const qreal currentAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
        const qreal angleDelta = normalizeAngleDelta(currentAngle - m_startAngle);
        emojiItem->setRotation(m_startRotation + angleDelta);
    } else if (m_isScaling && m_startDistance > 0.0) {
        const QPointF delta = QPointF(pos) - m_startCenter;
        const qreal currentDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
        emojiItem->setScale(m_startScale * (currentDistance / m_startDistance));
    } else {
        return false;
    }

    if (ctx && ctx->annotationLayer) {
        ctx->annotationLayer->invalidateCache();
    }
    return true;
}

bool EmojiStickerToolHandler::handleInteractionRelease(ToolContext* ctx,
                                                       const QPoint& pos,
                                                       Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(ctx);
    Q_UNUSED(pos);
    Q_UNUSED(modifiers);

    if (!m_isDragging && !m_isScaling && !m_isRotating) {
        return false;
    }

    m_isDragging = false;
    m_isScaling = false;
    m_isRotating = false;
    m_activeHandle = GizmoHandle::None;
    m_startDistance = 0.0;
    m_startAngle = 0.0;
    return true;
}

QRect EmojiStickerToolHandler::interactionBounds(const ToolContext* ctx) const
{
    auto* emojiItem = selectedEmoji(const_cast<ToolContext*>(ctx));
    if (!emojiItem) {
        return QRect();
    }

    return TransformationGizmo::visualBounds(emojiItem);
}

AnnotationInteractionKind EmojiStickerToolHandler::activeInteractionKind(const ToolContext* ctx) const
{
    Q_UNUSED(ctx);

    if (m_isDragging) {
        return AnnotationInteractionKind::SelectedDrag;
    }

    if (m_isScaling || m_isRotating) {
        return AnnotationInteractionKind::SelectedTransform;
    }

    return AnnotationInteractionKind::None;
}

bool EmojiStickerToolHandler::canHandleInteraction(ToolContext* ctx) const
{
    return ctx && ctx->annotationLayer;
}

EmojiStickerAnnotation* EmojiStickerToolHandler::selectedEmoji(ToolContext* ctx) const
{
    if (!ctx || !ctx->annotationLayer) {
        return nullptr;
    }
    return dynamic_cast<EmojiStickerAnnotation*>(ctx->annotationLayer->selectedItem());
}
