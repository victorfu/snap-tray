#ifndef MOSAICTOOLHANDLER_H
#define MOSAICTOOLHANDLER_H

#include "../IToolHandler.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/MosaicStroke.h"

#include <QVector>
#include <QPoint>
#include <QCursor>
#include <memory>

/**
 * @brief Tool handler for mosaic (pixelation) drawing.
 */
class MosaicToolHandler : public IToolHandler {
public:
    static constexpr int kDefaultBrushWidth = 18;
    static constexpr int kDefaultBlockSize = 6;

    MosaicToolHandler() = default;
    ~MosaicToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Mosaic; }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;

    void drawPreview(QPainter& painter) const override;
    bool isDrawing() const override { return m_isDrawing; }
    void cancelDrawing() override;

    bool supportsColor() const override { return false; }
    bool supportsWidth() const override { return true; }

    QCursor cursor() const override;

    /**
     * @brief Set the brush width for cursor display.
     *
     * This should be called before cursor() to ensure the correct
     * cursor size is returned. The width is automatically doubled
     * for display (UI shows half the actual drawing size).
     */
    void setWidth(int width);

private:
    bool m_isDrawing = false;
    QVector<QPoint> m_currentPath;
    std::unique_ptr<MosaicStroke> m_currentStroke;

    // Cursor caching (same pattern as EraserToolHandler)
    mutable QCursor m_cachedCursor;
    mutable int m_cachedCursorWidth = 0;
    int m_brushWidth = kDefaultBrushWidth;
};

#endif // MOSAICTOOLHANDLER_H
