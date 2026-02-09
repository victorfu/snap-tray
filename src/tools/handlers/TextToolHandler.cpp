#include "tools/handlers/TextToolHandler.h"

#include "tools/ToolContext.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/TextBoxAnnotation.h"
#include "region/TextAnnotationEditor.h"
#include "InlineTextEditor.h"
#include "TransformationGizmo.h"

#include <QDateTime>

void TextToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos)
{
    handleInteractionPress(ctx, pos, true);
}

void TextToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos)
{
    handleInteractionMove(ctx, pos);
}

void TextToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos)
{
    handleInteractionRelease(ctx, pos);
}

void TextToolHandler::onDoubleClick(ToolContext* ctx, const QPoint& pos)
{
    handleInteractionDoubleClick(ctx, pos);
}

bool TextToolHandler::handleEscape(ToolContext* ctx)
{
    if (!ctx) {
        return false;
    }

    if (ctx->inlineTextEditor && ctx->inlineTextEditor->isEditing()) {
        ctx->inlineTextEditor->cancelEditing();
        return true;
    }

    if (ctx->textAnnotationEditor && ctx->textAnnotationEditor->isTransforming()) {
        ctx->textAnnotationEditor->finishTransformation();
        ctx->repaint();
        return true;
    }

    if (ctx->textAnnotationEditor && ctx->textAnnotationEditor->isDragging()) {
        ctx->textAnnotationEditor->finishDragging();
        ctx->repaint();
        return true;
    }

    return false;
}

QCursor TextToolHandler::cursor() const
{
    return Qt::CrossCursor;
}

bool TextToolHandler::handleInteractionPress(ToolContext* ctx,
                                             const QPoint& pos,
                                             bool allowStartEditing)
{
    if (!canHandle(ctx)) {
        return false;
    }

    auto* layer = ctx->annotationLayer;
    auto* textEditor = ctx->textAnnotationEditor;

    auto* selectedText = dynamic_cast<TextBoxAnnotation*>(layer->selectedItem());
    if (selectedText) {
        GizmoHandle handle = TransformationGizmo::hitTest(selectedText, pos);
        if (handle != GizmoHandle::None) {
            if (handle == GizmoHandle::Body) {
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (textEditor->isDoubleClick(pos, now)) {
                    const int selectedIndex = layer->selectedIndex();
                    if (selectedIndex >= 0) {
                        textEditor->recordClick(QPoint(), 0);
                        return startReEditAtIndex(ctx, selectedIndex);
                    }
                }
                textEditor->recordClick(pos, now);
                textEditor->startDragging(pos);
            } else {
                textEditor->startTransformation(pos, handle);
            }

            if (ctx->requestHostFocus) {
                ctx->requestHostFocus();
            }
            ctx->repaint();
            return true;
        }
    }

    const int hitIndex = layer->hitTestText(pos);
    if (hitIndex >= 0) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (textEditor->isDoubleClick(pos, now)) {
            layer->setSelectedIndex(hitIndex);
            textEditor->recordClick(QPoint(), 0);
            return startReEditAtIndex(ctx, hitIndex);
        }

        textEditor->recordClick(pos, now);
        layer->setSelectedIndex(hitIndex);

        auto* hitText = dynamic_cast<TextBoxAnnotation*>(layer->selectedItem());
        if (hitText) {
            GizmoHandle handle = TransformationGizmo::hitTest(hitText, pos);
            if (handle == GizmoHandle::Body || handle == GizmoHandle::None) {
                textEditor->startDragging(pos);
            } else {
                textEditor->startTransformation(pos, handle);
            }
        }

        if (ctx->requestHostFocus) {
            ctx->requestHostFocus();
        }
        ctx->repaint();
        return true;
    }

    if (allowStartEditing && textEditor) {
        const QRect bounds = ctx->textEditingBounds.isValid()
            ? ctx->textEditingBounds
            : QRect();
        textEditor->startEditing(pos, bounds, ctx->color);
        ctx->repaint();
        return true;
    }

    return false;
}

bool TextToolHandler::handleInteractionMove(ToolContext* ctx, const QPoint& pos)
{
    if (!canHandle(ctx)) {
        return false;
    }

    auto* layer = ctx->annotationLayer;
    auto* textEditor = ctx->textAnnotationEditor;

    if (textEditor->isTransforming() && layer->selectedIndex() >= 0) {
        textEditor->updateTransformation(pos);
        layer->invalidateCache();
        ctx->repaint();
        return true;
    }

    if (textEditor->isDragging() && layer->selectedIndex() >= 0) {
        textEditor->updateDragging(pos);
        layer->invalidateCache();
        ctx->repaint();
        return true;
    }

    return false;
}

bool TextToolHandler::handleInteractionRelease(ToolContext* ctx, const QPoint& pos)
{
    Q_UNUSED(pos);

    if (!canHandle(ctx)) {
        return false;
    }

    auto* textEditor = ctx->textAnnotationEditor;
    if (textEditor->isTransforming()) {
        textEditor->finishTransformation();
        return true;
    }

    if (textEditor->isDragging()) {
        textEditor->finishDragging();
        return true;
    }

    return false;
}

bool TextToolHandler::handleInteractionDoubleClick(ToolContext* ctx, const QPoint& pos)
{
    if (!canHandle(ctx)) {
        return false;
    }

    const int hitIndex = ctx->annotationLayer->hitTestText(pos);
    if (hitIndex < 0) {
        return false;
    }

    ctx->annotationLayer->setSelectedIndex(hitIndex);
    return startReEditAtIndex(ctx, hitIndex);
}

bool TextToolHandler::startReEditAtIndex(ToolContext* ctx, int annotationIndex)
{
    if (!canHandle(ctx) || annotationIndex < 0) {
        return false;
    }

    auto* layer = ctx->annotationLayer;
    auto* textItem = dynamic_cast<TextBoxAnnotation*>(layer->itemAt(annotationIndex));
    if (!textItem) {
        return false;
    }

    if (ctx->syncColorToHost) {
        ctx->syncColorToHost(textItem->color());
    } else {
        ctx->color = textItem->color();
    }

    layer->invalidateCache();
    if (ctx->notifyTextReEditStarted) {
        ctx->notifyTextReEditStarted();
    }
    ctx->textAnnotationEditor->startReEditing(annotationIndex, ctx->color);
    ctx->repaint();
    return true;
}

bool TextToolHandler::canHandle(ToolContext* ctx) const
{
    return ctx && ctx->annotationLayer && ctx->textAnnotationEditor;
}
