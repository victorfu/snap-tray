#include "tools/handlers/StepBadgeToolHandler.h"
#include "tools/ToolContext.h"

#include <QCursor>

void StepBadgeToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    Q_UNUSED(ctx);
    Q_UNUSED(pos);
    // Step badge is placed on mouse release
}

void StepBadgeToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (!ctx->annotationLayer) {
        return;
    }

    // Get the next badge number
    int nextNumber = ctx->annotationLayer->countStepBadges() + 1;

    // Create and add the step badge
    auto badge = std::make_unique<StepBadgeAnnotation>(
        pos, ctx->color, nextNumber
    );

    ctx->addItem(std::move(badge));
    ctx->repaint();
}

QCursor StepBadgeToolHandler::cursor() const {
    return Qt::CrossCursor;
}
