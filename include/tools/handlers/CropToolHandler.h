#ifndef CROPTOOLHANDLER_H
#define CROPTOOLHANDLER_H

#include "../IToolHandler.h"

#include <QPoint>
#include <QRect>
#include <QSize>
#include <functional>

class CropToolHandler : public IToolHandler {
public:
    CropToolHandler() = default;
    ~CropToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Crop; }

    void onActivate(ToolContext* ctx) override;
    void onDeactivate(ToolContext* ctx) override;
    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;

    void drawPreview(QPainter& painter) const override;
    bool isDrawing() const override { return m_isDrawing; }
    void cancelDrawing() override;
    bool handleEscape(ToolContext* ctx) override;

    QCursor cursor() const override;

    void setCropCallback(std::function<void(const QRect&)> callback);
    void setImageSize(const QSize& size);

private:
    QRect makeRect(const QPoint& start, const QPoint& end) const;
    QRect clampToImage(const QRect& rect) const;

    bool m_isDrawing = false;
    QPoint m_startPoint;
    QPoint m_currentPoint;
    QSize m_imageSize;
    std::function<void(const QRect&)> m_cropCallback;

    static constexpr int kMinCropSize = 10;
    static constexpr int kHandleSize = 6;
};

#endif // CROPTOOLHANDLER_H
