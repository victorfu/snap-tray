#ifndef MOSAICTOOLHANDLER_H
#define MOSAICTOOLHANDLER_H

#include "../IToolHandler.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/MosaicStroke.h"

#include <QVector>
#include <QPoint>
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

private:
    bool m_isDrawing = false;
    QVector<QPoint> m_currentPath;
    std::unique_ptr<MosaicStroke> m_currentStroke;
};

#endif // MOSAICTOOLHANDLER_H
