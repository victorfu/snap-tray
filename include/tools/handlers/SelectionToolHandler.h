#ifndef SELECTIONTOOLHANDLER_H
#define SELECTIONTOOLHANDLER_H

#include "../IToolHandler.h"

/**
 * @brief Tool handler for selection mode.
 *
 * This is the default tool that allows selecting and manipulating
 * existing annotations (like text annotations).
 */
class SelectionToolHandler : public IToolHandler {
public:
    SelectionToolHandler() = default;
    ~SelectionToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Selection; }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;

    bool isDrawing() const override { return false; }

    bool supportsColor() const override { return false; }
    bool supportsWidth() const override { return false; }

    QCursor cursor() const override;
};

#endif // SELECTIONTOOLHANDLER_H
