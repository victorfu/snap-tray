#include "tools/handlers/StepBadgeToolHandler.h"
#include "tools/ToolContext.h"
#include "settings/AnnotationSettingsManager.h"

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

    // Get radius from settings (not ctx->width to avoid affecting brush width)
    StepBadgeSize size = AnnotationSettingsManager::instance().loadStepBadgeSize();
    int radius = StepBadgeAnnotation::radiusForSize(size);

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
