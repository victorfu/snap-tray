#include "qml/RecordingCursorOverlay.h"

#include "qml/RecordingPreviewBackend.h"
#include "recording/CursorEffects.h"

#include <QPainter>

RecordingCursorOverlay::RecordingCursorOverlay(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

QObject* RecordingCursorOverlay::backend() const
{
    return m_backend.data();
}

void RecordingCursorOverlay::setBackend(QObject* object)
{
    auto* backend = qobject_cast<RecordingPreviewBackend*>(object);
    if (m_backend == backend) return;
    if (m_backend) disconnect(m_backend, nullptr, this, nullptr);
    m_renderer.reset();
    m_cachedTimeline.reset();
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &RecordingPreviewBackend::cursorEffectsChanged, this, [this] { m_renderer.reset(); update(); });
        connect(m_backend, &RecordingPreviewBackend::videoSizeChanged, this, [this] { m_renderer.reset(); update(); });
        // "Still end" depends on where the clip ends.
        connect(m_backend, &RecordingPreviewBackend::trimRangeChanged, this, [this] { m_renderer.reset(); update(); });
        connect(m_backend, &RecordingPreviewBackend::durationChanged, this, [this] { m_renderer.reset(); update(); });
        connect(m_backend, &RecordingPreviewBackend::timelineChanged, this, [this] { m_renderer.reset(); update(); });
    }
    emit backendChanged();
    update();
}

void RecordingCursorOverlay::setPosition(qint64 position)
{
    if (m_position == position) return;
    m_position = position;
    emit positionChanged();
    update();
}

void RecordingCursorOverlay::setContentRect(const QRectF& rect)
{
    if (m_contentRect == rect) return;
    m_contentRect = rect;
    emit contentRectChanged();
    update();
}

void RecordingCursorOverlay::paint(QPainter* painter)
{
    if (!m_backend || !m_backend->cursorEffectsActive() || !m_backend->cursorTimeline()) return;
    const QSize videoSize = m_backend->videoSize();
    if (videoSize.isEmpty() || m_contentRect.isEmpty()) return;
    // Draw in video pixels, then let the painter scale to the shown rect, so
    // the geometry is exactly the export's (the preview shows the full frame).
    painter->save();
    painter->setClipRect(m_contentRect);
    painter->translate(m_contentRect.topLeft());
    painter->scale(m_contentRect.width() / videoSize.width(), m_contentRect.height() / videoSize.height());
    if (!m_renderer) {
        m_cachedTimeline = m_backend->cursorTimelineSnapshot();
        m_renderer = std::make_unique<SnapTray::CursorEffectRenderer>(
            *m_cachedTimeline, m_backend->cursorEffects(), m_backend->effectsClipEnd());
        if (!m_backend->timelineMap().isEmpty()) m_renderer->setTimeline(m_backend->timelineMap());
    }
    m_renderer->paint(*painter, m_position, {QRect(), videoSize, videoSize});
    painter->restore();
}
