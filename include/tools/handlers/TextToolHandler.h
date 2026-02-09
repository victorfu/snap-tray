#ifndef TEXTTOOLHANDLER_H
#define TEXTTOOLHANDLER_H

#include "../IToolHandler.h"

class ToolContext;

/**
 * @brief Unified text tool handler.
 *
 * Handles:
 * - Text creation when Text tool is active
 * - Text selection/drag/transform/re-edit interactions
 *
 * The same interaction path can be invoked globally by ToolManager so text
 * manipulation remains available even when another tool is active.
 */
class TextToolHandler : public IToolHandler {
public:
    TextToolHandler() = default;
    ~TextToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Text; }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;
    void onDoubleClick(ToolContext* ctx, const QPoint& pos) override;

    bool handleEscape(ToolContext* ctx) override;

    bool supportsColor() const override { return true; }
    bool supportsTextFormatting() const override { return true; }
    QCursor cursor() const override;

    // Global interaction entry points used by ToolManager regardless of current tool.
    bool handleInteractionPress(ToolContext* ctx, const QPoint& pos, bool allowStartEditing);
    bool handleInteractionMove(ToolContext* ctx, const QPoint& pos);
    bool handleInteractionRelease(ToolContext* ctx, const QPoint& pos);
    bool handleInteractionDoubleClick(ToolContext* ctx, const QPoint& pos);

private:
    bool startReEditAtIndex(ToolContext* ctx, int annotationIndex);
    bool canHandle(ToolContext* ctx) const;
};

#endif // TEXTTOOLHANDLER_H
