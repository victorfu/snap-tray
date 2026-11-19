#ifndef MEASURETOOLHANDLER_H
#define MEASURETOOLHANDLER_H

#include "../IToolHandler.h"
#include "tools/MeasureEdgeScanner.h"

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>

class QFontMetrics;
class TestMeasureToolHandler;
class TestPinWindowMeasure;

/**
 * @brief Measure tool: a dragged line, plus an optional image-based mode (#348).
 *
 * Manual: drag a line; Shift keeps it horizontal or vertical. Auto: hovering
 * also shows the horizontal and vertical spans the image edges give around
 * the pointer. Dragging can snap line endpoints or (with Control) box sides
 * to image edges. Measurements derived from those edges are marked as estimates.
 *
 * Pointer input arrives in host tool coordinates. setImageGeometry() maps
 * them to the image's own pixels so values do not change with the pin's zoom,
 * and the image's device pixel ratio gives logical units next to pixels.
 */
class MeasureToolHandler : public IToolHandler {
public:
    // Returns the image under the tool in its own pixels (Format_ARGB32 or
    // Format_RGB32), the size given to setImageGeometry(). The host keeps it
    // cached for hover and drag sampling.
    using RasterProvider = std::function<std::shared_ptr<const QImage>()>;

    MeasureToolHandler() = default;
    ~MeasureToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Measure; }

    void onActivate(ToolContext* ctx) override;
    void onDeactivate(ToolContext* ctx) override;
    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;
    // Pointer input keeps its fraction: at a device pixel ratio above 1 one
    // tool unit covers several image pixels, for hovers and drags alike.
    void onMousePressF(ToolContext* ctx, const QPointF& pos) override;
    void onMouseMoveF(ToolContext* ctx, const QPointF& pos) override;
    void onMouseReleaseF(ToolContext* ctx, const QPointF& pos) override;

    void drawPreview(QPainter& painter) const override;
    bool isDrawing() const override { return m_isDrawing; }
    void cancelDrawing() override;
    bool handleEscape(ToolContext* ctx) override;

    QCursor cursor() const override { return Qt::CrossCursor; }

    void setAutoMode(bool enabled);
    bool autoMode() const { return m_autoMode; }

    /// @p toolSize is the image extent in tool coordinates, @p pixelSize the
    /// same image in its own pixels, @p imageDpr its device pixel ratio and
    /// @p imageKey identifies its content (a new key drops shown spans).
    void setImageGeometry(const QSize& toolSize, const QSize& pixelSize, qreal imageDpr,
                          qint64 imageKey = 0);
    /// @p release is called when the tool no longer needs the image (tool
    /// switched away, manual mode), so the host can free its copy.
    void setRasterProvider(RasterProvider provider, std::function<void()> release = {});
    qreal imageDevicePixelRatio() const { return m_imageDpr; }

    /// Hides the hover spans (pointer left, image changed). True if any were shown.
    bool clearHover();
    bool hasHoverMeasurement() const { return m_hoverHorizontal || m_hoverVertical; }
    std::optional<MeasureEdgeScanner::Span> hoverSpan(Qt::Orientation orientation) const;

    /// "80 px", or "80 px · 40 pt" when the image has a device pixel ratio.
    static QString formatLength(int pixels, qreal imageDpr);
    /// One line in pixels, plus one in logical units when they differ.
    static QStringList manualLabelLines(const QPointF& deltaPixels, qreal imageDpr);

private:
    friend class TestMeasureToolHandler;
    friend class TestPinWindowMeasure;

    QPointF constrainEndpoint(const QPointF& start, const QPointF& end, bool shiftHeld) const;
    void updateDrag(const QPointF& imageEnd, bool shiftHeld);
    QStringList measurementLabelLines(const QPointF& deltaPixels) const;
    // p1 and p2 are in image pixels.
    void drawMeasurementOverlay(QPainter& painter, const QPointF& p1, const QPointF& p2) const;
    void drawHoverOverlay(QPainter& painter) const;
    // Background rect of a label whose text block starts at @p textTopCenter.
    QRect labelRect(const QFontMetrics& fm, const QStringList& lines, const QPoint& textTopCenter) const;
    // Moves @p rect inside the image (the whole pin) when the geometry is known.
    QRect keepInsideImage(QRect rect) const;
    // Where the hover label of @p orientation goes: beside its span line,
    // on the side with room, kept inside the pin.
    QRect hoverLabelRect(Qt::Orientation orientation, const QFontMetrics& fm) const;
    QString hoverLabelText(Qt::Orientation orientation) const;
    void paintLabel(QPainter& painter, const QStringList& lines, const QRect& backgroundRect) const;

    bool updateHover(const QPointF& toolPos);
    QPoint toPixel(const QPointF& toolPos) const;
    // Tool coordinates <-> image pixels (fractional, unrounded).
    QPointF toImage(const QPointF& toolPos) const;
    QPointF toTool(const QPointF& imagePos) const;
    void releaseRaster();
    QPointF pixelsPerToolUnit() const;
    bool hasImageGeometry() const;

    // Endpoints are kept in image pixels: zooming the pin moves the line with
    // the image instead of changing what it measures.
    bool m_isDrawing = false;
    QPointF m_rawStart;
    QPointF m_startPoint;
    QPointF m_endPoint;
    bool m_isBox = false;
    bool m_isEstimate = false;

    bool m_hasCompletedMeasurement = false;
    QPointF m_completedStart;
    QPointF m_completedEnd;

    bool m_autoMode = false;
    QSize m_toolSize;
    QSize m_pixelSize;
    qreal m_imageDpr = 1.0;
    qint64 m_imageKey = 0;
    RasterProvider m_rasterProvider;
    std::function<void()> m_releaseRaster;

    QPoint m_hoverPixel{-1, -1};
    std::shared_ptr<const QImage> m_hoverRaster;   // the image the spans came from
    std::optional<MeasureEdgeScanner::Span> m_hoverHorizontal;
    std::optional<MeasureEdgeScanner::Span> m_hoverVertical;

    static constexpr int kEndpointRadius = 4;
    static constexpr int kMinMeasureDistance = 3;
    static constexpr int kLabelOffsetY = 20;
    static constexpr int kLabelPaddingH = 6;
    static constexpr int kLabelPaddingV = 2;
    static constexpr int kLabelRadius = 4;
    static constexpr int kSpanTickHalfLength = 5;
    static constexpr int kSpanLabelGap = 8;
};

#endif // MEASURETOOLHANDLER_H
