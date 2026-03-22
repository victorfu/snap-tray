#ifndef ITOOLHANDLER_H
#define ITOOLHANDLER_H

#include <QPoint>
#include <QCursor>
#include <memory>

#include "ToolId.h"
#include "cursor/CursorTypes.h"

class QPainter;
class ToolContext;

enum class AnnotationInteractionKind {
    None,
    SelectedDrag,
    SelectedTransform
};

/**
 * @brief Abstract interface for tool behavior handlers.
 *
 * Each tool implements this interface to define its specific
 * drawing behavior. The ToolManager dispatches events to the
 * appropriate handler based on the current tool.
 */
class IToolHandler {
public:
    virtual ~IToolHandler() = default;

    /**
     * @brief Get the tool ID this handler is responsible for.
     */
    virtual ToolId toolId() const = 0;

    /**
     * @brief Called when this tool becomes active.
     */
    virtual void onActivate(ToolContext* ctx) { Q_UNUSED(ctx); }

    /**
     * @brief Called when this tool is deactivated.
     */
    virtual void onDeactivate(ToolContext* ctx) { Q_UNUSED(ctx); }

    /**
     * @brief Called when mouse button is pressed.
     */
    virtual void onMousePress(ToolContext* ctx, const QPoint& pos) {
        Q_UNUSED(ctx);
        Q_UNUSED(pos);
    }

    /**
     * @brief Called when mouse is moved (while pressed or not).
     */
    virtual void onMouseMove(ToolContext* ctx, const QPoint& pos) {
        Q_UNUSED(ctx);
        Q_UNUSED(pos);
    }

    /**
     * @brief Called when mouse button is released.
     */
    virtual void onMouseRelease(ToolContext* ctx, const QPoint& pos) {
        Q_UNUSED(ctx);
        Q_UNUSED(pos);
    }

    /**
     * @brief Called when mouse is double-clicked.
     */
    virtual void onDoubleClick(ToolContext* ctx, const QPoint& pos) {
        Q_UNUSED(ctx);
        Q_UNUSED(pos);
    }

    /**
     * @brief Draw the current in-progress annotation preview.
     */
    virtual void drawPreview(QPainter& painter) const { Q_UNUSED(painter); }

    /**
     * @brief Bounds of the in-progress preview in annotation/widget logical coordinates.
     *
     * When invalid, ToolManager falls back to full repaint signaling.
     */
    virtual QRect previewBounds(const ToolContext* ctx) const
    {
        Q_UNUSED(ctx);
        return QRect();
    }

    /**
     * @brief Check if currently drawing an annotation.
     */
    virtual bool isDrawing() const { return false; }

    /**
     * @brief Cancel current drawing operation.
     */
    virtual void cancelDrawing() {}

    /**
     * @brief Handle Escape key press.
     * @return true if the event was handled (should not close app), false otherwise.
     */
    virtual bool handleEscape(ToolContext* ctx) { 
        Q_UNUSED(ctx);
        return false; 
    }

    /**
     * @brief Optional shared selected-annotation interaction hooks.
     */
    virtual bool handleInteractionPress(ToolContext* ctx,
                                        const QPoint& pos,
                                        Qt::KeyboardModifiers modifiers)
    {
        Q_UNUSED(ctx);
        Q_UNUSED(pos);
        Q_UNUSED(modifiers);
        return false;
    }

    virtual bool handleInteractionMove(ToolContext* ctx,
                                       const QPoint& pos,
                                       Qt::KeyboardModifiers modifiers)
    {
        Q_UNUSED(ctx);
        Q_UNUSED(pos);
        Q_UNUSED(modifiers);
        return false;
    }

    virtual bool handleInteractionRelease(ToolContext* ctx,
                                          const QPoint& pos,
                                          Qt::KeyboardModifiers modifiers)
    {
        Q_UNUSED(ctx);
        Q_UNUSED(pos);
        Q_UNUSED(modifiers);
        return false;
    }

    virtual bool handleInteractionDoubleClick(ToolContext* ctx, const QPoint& pos)
    {
        Q_UNUSED(ctx);
        Q_UNUSED(pos);
        return false;
    }

    /**
     * @brief Bounds of the active selected-annotation interaction in annotation coordinates.
     */
    virtual QRect interactionBounds(const ToolContext* ctx) const
    {
        Q_UNUSED(ctx);
        return QRect();
    }

    /**
     * @brief Active interaction kind for a selected annotation, if any.
     */
    virtual AnnotationInteractionKind activeInteractionKind(const ToolContext* ctx) const
    {
        Q_UNUSED(ctx);
        return AnnotationInteractionKind::None;
    }

    /**
     * @brief Check if this tool supports color selection.
     */
    virtual bool supportsColor() const { return false; }

    /**
     * @brief Check if this tool supports width/stroke adjustment.
     */
    virtual bool supportsWidth() const { return false; }

    /**
     * @brief Check if this tool supports text formatting.
     */
    virtual bool supportsTextFormatting() const { return false; }

    /**
     * @brief Check if this tool supports arrow style selection.
     */
    virtual bool supportsArrowStyle() const { return false; }

    /**
     * @brief Check if this tool supports line style selection.
     */
    virtual bool supportsLineStyle() const { return false; }

    /**
     * @brief Check if this tool supports shape type selection.
     */
    virtual bool supportsShapeType() const { return false; }

    /**
     * @brief Check if this tool supports fill mode selection.
     */
    virtual bool supportsFillMode() const { return false; }

    /**
     * @brief Get the cursor for this tool.
     */
    virtual QCursor cursor() const { return Qt::CrossCursor; }

    /**
     * @brief Semantic cursor description used by CursorAuthority.
     *
     * Temporary compatibility layer during cursor-system migration.
     */
    virtual CursorStyleSpec cursorStyleSpec() const { return CursorStyleSpec::fromCursor(cursor()); }
};

#endif // ITOOLHANDLER_H
