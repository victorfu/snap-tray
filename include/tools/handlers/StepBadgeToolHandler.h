#ifndef STEPBADGETOOLHANDLER_H
#define STEPBADGETOOLHANDLER_H

#include "../IToolHandler.h"
#include "../../AnnotationLayer.h"

/**
 * @brief Tool handler for step badge (numbered circle) placement.
 */
class StepBadgeToolHandler : public IToolHandler {
public:
    StepBadgeToolHandler() = default;
    ~StepBadgeToolHandler() override = default;

    ToolId toolId() const override { return ToolId::StepBadge; }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;

    bool isDrawing() const override { return false; }

    bool supportsColor() const override { return true; }
    bool supportsWidth() const override { return false; }

    QCursor cursor() const override;
};

#endif // STEPBADGETOOLHANDLER_H
