#ifndef EMOJISTICKERTOOLHANDLER_H
#define EMOJISTICKERTOOLHANDLER_H

#include "../IToolHandler.h"
#include "TransformationGizmo.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/EmojiStickerAnnotation.h"

class EmojiStickerToolHandler : public IToolHandler {
public:
    EmojiStickerToolHandler() = default;
    ~EmojiStickerToolHandler() override = default;

    ToolId toolId() const override { return ToolId::EmojiSticker; }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;
    bool handleInteractionPress(ToolContext* ctx, const QPoint& pos, Qt::KeyboardModifiers modifiers) override;
    bool handleInteractionMove(ToolContext* ctx, const QPoint& pos, Qt::KeyboardModifiers modifiers) override;
    bool handleInteractionRelease(ToolContext* ctx, const QPoint& pos, Qt::KeyboardModifiers modifiers) override;
    QRect interactionBounds(const ToolContext* ctx) const override;
    AnnotationInteractionKind activeInteractionKind(const ToolContext* ctx) const override;

    bool isDrawing() const override { return false; }

    bool supportsColor() const override { return false; }
    bool supportsWidth() const override { return false; }

    QCursor cursor() const override;

    void setCurrentEmoji(const QString& emoji) { m_currentEmoji = emoji; }
    QString currentEmoji() const { return m_currentEmoji; }

private:
    bool canHandleInteraction(ToolContext* ctx) const;
    EmojiStickerAnnotation* selectedEmoji(ToolContext* ctx) const;

    QString m_currentEmoji = QString::fromUtf8("\xF0\x9F\x98\x80");  // Default: 😀
    bool m_isDragging = false;
    bool m_isScaling = false;
    bool m_isRotating = false;
    GizmoHandle m_activeHandle = GizmoHandle::None;
    QPoint m_dragStart;
    qreal m_startScale = 1.0;
    qreal m_startDistance = 0.0;
    QPointF m_startCenter;
    qreal m_startRotation = 0.0;
    qreal m_startAngle = 0.0;
};

#endif // EMOJISTICKERTOOLHANDLER_H
