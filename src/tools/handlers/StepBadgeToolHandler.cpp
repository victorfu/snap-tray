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

    // Get radius from context width (repurposed for step badge size)
    // Default to medium if not set
    int radius = ctx->width;
    if (radius <= 0) {
        radius = StepBadgeAnnotation::kBadgeRadiusMedium;
    }

    // Create and add the step badge with custom radius
    auto badge = std::make_unique<StepBadgeAnnotation>(
        pos, ctx->color, nextNumber, radius
    );

    ctx->addItem(std::move(badge));
    ctx->repaint();
}

QCursor StepBadgeToolHandler::cursor() const {
    return Qt::CrossCursor;
}
