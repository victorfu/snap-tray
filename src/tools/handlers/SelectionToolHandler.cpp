#include "tools/handlers/SelectionToolHandler.h"
#include "tools/ToolContext.h"

#include <QCursor>

void SelectionToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    // Selection tool behavior is typically handled by the parent widget
    // since it involves hit-testing text annotations and managing
    // selection state that's specific to the UI.
    Q_UNUSED(ctx);
    Q_UNUSED(pos);
}

void SelectionToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    Q_UNUSED(ctx);
    Q_UNUSED(pos);
}

void SelectionToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    Q_UNUSED(ctx);
    Q_UNUSED(pos);
}

QCursor SelectionToolHandler::cursor() const {
    return Qt::ArrowCursor;
}
