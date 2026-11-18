#ifndef CROPTOOLHANDLER_H
#define CROPTOOLHANDLER_H

#include "../CropDraftModel.h"
#include "../IToolHandler.h"

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <functional>

class TestCropToolHandler;

/**
 * @brief Crop tool with an adjustable draft (#358).
 *
 * Dragging only makes a draft selection. It can then be moved, resized by its
 * handles, nudged from the keyboard and given an aspect ratio; nothing is
 * cropped until commitDraft(). The draft lives in source pixels
 * (CropDraftModel); pointer input arrives in the host's tool coordinates and
 * is converted with the sizes given by setImageSize() and setSource().
 */
class CropToolHandler : public IToolHandler {
public:
    using Aspect = CropDraftModel::Aspect;
    using Handle = CropDraftModel::Handle;
    // Receives the draft in source pixels; returns true once it was applied.
    using CropCallback = std::function<bool(const QRect&)>;

    CropToolHandler() = default;
    ~CropToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Crop; }

    void onActivate(ToolContext* ctx) override;
    void onDeactivate(ToolContext* ctx) override;
    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;
    void onMousePressF(ToolContext* ctx, const QPointF& pos) override;
    void onMouseMoveF(ToolContext* ctx, const QPointF& pos) override;
    void onMouseReleaseF(ToolContext* ctx, const QPointF& pos) override;

    void drawPreview(QPainter& painter) const override;
    // True only while a pointer drag is in progress, not for an idle draft.
    bool isDrawing() const override { return m_mode != Mode::Idle; }
    void cancelDrawing() override;
    bool handleEscape(ToolContext* ctx) override;

    QCursor cursor() const override;

    void setCropCallback(CropCallback callback);
    // Called whenever the draft, its size or the aspect preset changes.
    void setDraftChangedCallback(std::function<void()> callback);

    // Size of the tool coordinate space the pointer positions are in.
    void setImageSize(const QSize& size);
    // The image being cropped: its pixel size, device pixel ratio and an
    // identity token. A different size or token discards the draft, so a
    // selection never carries over to another image.
    void setSource(const QSize& pixelSize, qreal devicePixelRatio, qint64 token);
    // How the host shows the image, for preset orientation and resize cursors.
    void setViewOrientation(int rotationAngle, bool flipHorizontal, bool flipVertical);

    bool hasDraft() const { return m_model.hasRect(); }
    QRect draftRect() const { return m_model.rect(); }
    QRectF draftToolRect() const;
    bool draftContains(const QPointF& toolPos) const;

    Aspect aspect() const { return m_model.aspect(); }
    void setAspect(Aspect aspect);

    // See CropDraftModel::setPositionStep().
    void setPositionStep(const QSize& sourceStep);
    QSize positionStep() const { return m_model.positionStep(); }

    // Moves the draft by position steps (one source pixel each unless the
    // host asked for a coarser grid). False without a draft.
    bool nudgeDraft(const QPoint& steps);
    // Applies the draft through the crop callback. False when there is no
    // draft, a drag is in progress, or the host refused.
    bool commitDraft();
    void discardDraft();

private:
    friend class TestCropToolHandler;

    enum class Mode { Idle, Drawing, Moving, Resizing };

    QPoint toSource(const QPointF& toolPos) const;
    Handle hitTest(const QPointF& toolPos) const;
    Qt::CursorShape cursorForHandle(Handle handle) const;
    void notifyDraftChanged();
    bool canInteract() const;

    CropDraftModel m_model;
    Mode m_mode = Mode::Idle;
    Handle m_activeHandle = Handle::None;
    Handle m_hoverHandle = Handle::None;
    QPointF m_pressToolPos;
    QPoint m_pressSourcePos;
    bool m_dragExceededThreshold = false;

    QSize m_imageSize;
    qint64 m_sourceToken = 0;
    int m_rotationAngle = 0;
    bool m_flipHorizontal = false;
    bool m_flipVertical = false;

    CropCallback m_cropCallback;
    std::function<void()> m_draftChangedCallback;

    // Pointer travel, in tool pixels, before a press starts a new selection.
    static constexpr qreal kDragThreshold = 3.0;
    static constexpr int kHandleSize = 8;
    static constexpr qreal kHandleHitRadius = 8.0;
    // Share of the draft, from each edge inward, that belongs to its handles.
    static constexpr qreal kHandleInnerFraction = 0.25;
};

#endif // CROPTOOLHANDLER_H
