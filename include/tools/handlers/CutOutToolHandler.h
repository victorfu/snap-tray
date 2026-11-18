#ifndef CUTOUTTOOLHANDLER_H
#define CUTOUTTOOLHANDLER_H

#include "../IToolHandler.h"
#include "../StripDraftModel.h"

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <functional>

/**
 * @brief Cut-out tool: marks a band across the image to remove, or a blank
 * band to insert (#371).
 *
 * Like the crop tool it only keeps a draft (StripDraftModel, in source
 * pixels); the host changes the image when commitDraft() is called. Pointer
 * input arrives in the host's tool coordinates.
 */
class CutOutToolHandler : public IToolHandler {
public:
    using Axis = ImageStrip::Axis;
    using Operation = ImageStrip::Operation;
    using Part = StripDraftModel::Part;
    // Receives the strip in source pixels; returns true once it was applied.
    using CommitCallback = std::function<bool(const ImageStrip::Request&)>;

    ToolId toolId() const override { return ToolId::CutOut; }

    void onActivate(ToolContext* ctx) override;
    void onDeactivate(ToolContext* ctx) override;
    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;
    void onMousePressF(ToolContext* ctx, const QPointF& pos) override;
    void onMouseMoveF(ToolContext* ctx, const QPointF& pos) override;
    void onMouseReleaseF(ToolContext* ctx, const QPointF& pos) override;

    void drawPreview(QPainter& painter) const override;
    bool isDrawing() const override { return m_mode != Mode::Idle; }
    void cancelDrawing() override;
    bool handleEscape(ToolContext* ctx) override;
    QCursor cursor() const override;

    void setCommitCallback(CommitCallback callback);
    void setDraftChangedCallback(std::function<void()> callback);

    void setImageSize(const QSize& size);
    // A different size or token (another image) discards the draft.
    void setSource(const QSize& pixelSize, qreal devicePixelRatio, qint64 token);
    void setViewOrientation(int rotationAngle, bool flipHorizontal, bool flipVertical);

    bool hasDraft() const { return m_model.hasStrip(); }
    // False when no strip can be applied exactly at the current zoom.
    bool canMarkStrip() const { return m_model.lengthStep() > 0; }
    ImageStrip::Request request() const { return m_model.request(); }
    QSize resultSize() const { return m_model.resultSize(); }
    QRectF draftToolRect() const;
    bool draftContains(const QPointF& toolPos) const;

    // The band's direction as the user sees the image: Rows is a horizontal
    // band on screen even when the pin is rotated by a quarter turn.
    Axis viewAxis() const;
    void setViewAxis(Axis axis);
    Operation operation() const { return m_model.operation(); }
    void setOperation(Operation operation);

    // Moves the band along its axis by whole source pixels.
    bool nudgeDraft(int sourceDelta);
    bool commitDraft();
    void discardDraft();

private:
    enum class Mode { Idle, Drawing, Moving, Resizing };

    bool canInteract() const;
    bool transposed() const { return m_rotationAngle == 90 || m_rotationAngle == 270; }
    qreal toolExtent() const;
    qreal toolPosAlong(const QPointF& toolPos) const;
    int toSource(const QPointF& toolPos) const;
    Part hitTest(const QPointF& toolPos) const;
    void notifyDraftChanged();
    void resetGesture();

    StripDraftModel m_model;
    Mode m_mode = Mode::Idle;
    Part m_activePart = Part::None;
    Part m_hoverPart = Part::None;
    QPointF m_pressToolPos;
    int m_pressSourcePos = 0;
    int m_startBeforeGesture = 0;
    int m_lengthBeforeGesture = 0;
    bool m_dragExceededThreshold = false;

    QSize m_imageSize;
    qint64 m_sourceToken = 0;
    int m_rotationAngle = 0;
    bool m_flipHorizontal = false;
    bool m_flipVertical = false;

    CommitCallback m_commitCallback;
    std::function<void()> m_draftChangedCallback;

    static constexpr qreal kDragThreshold = 3.0;
    static constexpr qreal kEdgeHitRadius = 6.0;
    // Share of the band, from each edge inward, that resizes instead of moves.
    static constexpr qreal kEdgeInnerFraction = 0.25;
};

#endif // CUTOUTTOOLHANDLER_H
