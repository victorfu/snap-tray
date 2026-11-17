#include "recording/RecordingFrameFilter.h"
#include <QPainter>

namespace SnapTray {
std::function<void(QImage&, qint64, qint64)> recordingFrameFilter(
    const PreviewEditState& state, const QSize& videoSize, const TimelineMap& editTimeline,
    std::shared_ptr<const CursorTimeline> timeline, std::shared_ptr<const KeyTimeline> keyTimeline)
{
    const QRectF bounds = state.crop.isEmpty() ? QRectF(QPointF(), QSizeF(videoSize)) : QRectF(state.crop);
    const auto availability = CursorEffectAvailability::of(timeline.get());
    const auto cursor = availability.cursor && (state.cursor.showCursor || (state.cursor.clickRipple && availability.clicks))
        ? std::make_shared<CursorEffectRenderer>(*timeline, state.cursor,
              editTimeline.isEmpty() ? -1 : editTimeline.sourceEndMs())
        : std::shared_ptr<CursorEffectRenderer>();
    if (cursor && !editTimeline.isEmpty()) cursor->setTimeline(editTimeline);
    std::shared_ptr<ZoomTrack> zoom;
    if (state.zoom.isActive()) {
        zoom = std::make_shared<ZoomTrack>(state.zoom);
        if (timeline) zoom->prepareFollow(*timeline, state.cursor, editTimeline, bounds);
    }
    const auto redaction = state.redaction.isEmpty() ? std::shared_ptr<RedactionTrack>()
        : std::make_shared<RedactionTrack>(state.redaction);
    const auto appearance = state.appearance.enabled && !bounds.isEmpty()
        ? std::make_shared<const RecordingAppearanceRenderer>(state.appearance, bounds.size().toSize(),
              state.appearance.background == RecordingAppearance::Background::Image
                  ? QImage(state.appearance.imagePath) : QImage())
        : std::shared_ptr<const RecordingAppearanceRenderer>();
    const bool blurredBackground = appearance
        && state.appearance.background == RecordingAppearance::Background::BlurredVideo;
    std::shared_ptr<KeyOverlayRenderer> keys;
    if (keyTimeline && state.keys.enabled) {
        keys = std::shared_ptr<KeyOverlayRenderer>(new KeyOverlayRenderer(*keyTimeline, state.keys),
            [held = keyTimeline](KeyOverlayRenderer* renderer) { delete renderer; });
        keys->setTimeline(editTimeline);
    }
    const auto overlays = state.overlays.isEmpty() ? std::shared_ptr<const OverlayTrack>()
        : std::make_shared<const OverlayTrack>(state.overlays);
    return [timeline, editTimeline, cursor, zoom, redaction, appearance, blurredBackground, keys, overlays, bounds, videoSize](
               QImage& frame, qint64 sourceMs, qint64 sourceEndMs) {
        if (frame.format() != QImage::Format_RGB32 && frame.format() != QImage::Format_ARGB32_Premultiplied) {
            frame = frame.convertToFormat(QImage::Format_RGB32);
        }
        if (frame.isNull()) return;
        // Masks first, on the source content: nothing they cover can come
        // back through the zoom's resampling.
        // Never skip masks for lack of geometry: without a known size the
        // frame itself is the source area.
        if (redaction) {
            // Preserve conservative lookahead within the current kept part only.
            // Neither a held frame nor the legacy guard may include a deleted gap.
            qint64 maskEndMs = qMax(sourceEndMs, sourceMs + SnapTray::RedactionTrack::kFrameHoldMs);
            const int part = editTimeline.segmentAt(sourceMs);
            if (part >= 0)
                maskEndMs = qMin(maskEndMs, editTimeline.segments()[size_t(part)].endMs);
            redaction->apply(frame, sourceMs,
                             bounds.isEmpty() ? QRectF(QPointF(0, 0), QSizeF(frame.size())) : bounds, maskEndMs);
        }
        if (frame.isNull()) return;
        // A blurred background comes from the masked picture, before zoom and
        // pointer: nothing a mask covers can show through it.
        const QImage blurSource = blurredBackground
            ? SnapTray::RecordingAppearanceRenderer::blurSourceFor(frame) : QImage();
        // The frame shows `bounds`; a zoom shows part of it, scaled to fill.
        QRectF shown = bounds;
        if (zoom) {
            shown = zoom->viewportAt(sourceMs, bounds);
            if (shown != bounds) {
                QImage zoomed(frame.size(), frame.format());
                if (zoomed.isNull()) { frame = {}; return; }
                QPainter painter(&zoomed);
                if (!painter.isActive()) { frame = {}; return; }
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                const double sx = frame.width() / bounds.width();
                const double sy = frame.height() / bounds.height();
                const QRectF source((shown.x() - bounds.x()) * sx, (shown.y() - bounds.y()) * sy,
                                    shown.width() * sx, shown.height() * sy);
                painter.drawImage(QRectF(zoomed.rect()), frame, source);
                painter.end();
                frame = zoomed;
            }
        }
        if (cursor) cursor->paint(frame, sourceMs, {shown, videoSize, frame.size()});
        if (keys) keys->paint(frame, sourceMs);
        if (overlays) overlays->paint(frame, sourceMs);
        if (appearance) frame = appearance->compose(frame, blurSource);
    };
}

} // namespace SnapTray
