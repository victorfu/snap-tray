#ifndef EMOJISTICKERTOOLHANDLER_H
#define EMOJISTICKERTOOLHANDLER_H

#include "../IToolHandler.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/EmojiStickerAnnotation.h"

class EmojiStickerToolHandler : public IToolHandler {
public:
    EmojiStickerToolHandler() = default;
    ~EmojiStickerToolHandler() override = default;

    ToolId toolId() const override { return ToolId::EmojiSticker; }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;

    bool isDrawing() const override { return false; }

    bool supportsColor() const override { return false; }
    bool supportsWidth() const override { return false; }

    QCursor cursor() const override;

    void setCurrentEmoji(const QString& emoji) { m_currentEmoji = emoji; }
    QString currentEmoji() const { return m_currentEmoji; }

private:
    QString m_currentEmoji = QString::fromUtf8("\xF0\x9F\x98\x80");  // Default: 😀
};

#endif // EMOJISTICKERTOOLHANDLER_H
