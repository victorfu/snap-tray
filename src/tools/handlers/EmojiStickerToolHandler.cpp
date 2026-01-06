#include "tools/handlers/EmojiStickerToolHandler.h"
#include "tools/ToolContext.h"

#include <QCursor>

void EmojiStickerToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    Q_UNUSED(ctx);
    Q_UNUSED(pos);
}

void EmojiStickerToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (!ctx->annotationLayer) {
        return;
    }

    if (m_currentEmoji.isEmpty()) {
        return;
    }

    auto sticker = std::make_unique<EmojiStickerAnnotation>(pos, m_currentEmoji);

    ctx->addItem(std::move(sticker));
    ctx->repaint();
}

QCursor EmojiStickerToolHandler::cursor() const {
    return Qt::CrossCursor;
}
