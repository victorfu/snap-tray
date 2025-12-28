#ifndef ERASERTOOLHANDLER_H
#define ERASERTOOLHANDLER_H

#include "../IToolHandler.h"
#include <QPoint>
#include <QPixmap>

class EraserStroke;

/**
 * @brief Tool handler for erasing annotations.
 *
 * Supports adjustable eraser width (5-100 pixels) and provides
 * visual feedback during hover and active erasing.
 */
class EraserToolHandler : public IToolHandler {
public:
    EraserToolHandler() = default;
    ~EraserToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Eraser; }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;

    void drawPreview(QPainter& painter) const override;
    bool isDrawing() const override { return m_isDrawing; }
    void cancelDrawing() override;

    bool supportsColor() const override { return false; }
    bool supportsWidth() const override { return true; }

    QCursor cursor() const override;

    // Width control
    void setWidth(int width);
    int width() const { return m_eraserWidth; }

    // Hover preview support
    void setHoverPoint(const QPoint& pos);
    void clearHoverPoint();

    static constexpr int kDefaultWidth = 20;
    static constexpr int kMinWidth = 5;
    static constexpr int kMaxWidth = 100;

private:
    QPixmap createCursorPixmap() const;

    bool m_isDrawing = false;
    EraserStroke* m_activeStroke = nullptr;
    QPoint m_lastPoint;

    int m_eraserWidth = kDefaultWidth;

    // Hover tracking
    QPoint m_hoverPoint;
    bool m_hasHoverPoint = false;

    // Cached cursor (regenerated when width changes)
    mutable QCursor m_cachedCursor;
    mutable int m_cachedCursorWidth = 0;
};

#endif // ERASERTOOLHANDLER_H
