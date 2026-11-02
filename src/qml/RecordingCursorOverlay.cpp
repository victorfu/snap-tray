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
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &RecordingPreviewBackend::cursorEffectsChanged, this, [this] { update(); });
        connect(m_backend, &RecordingPreviewBackend::videoSizeChanged, this, [this] { update(); });
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
    const SnapTray::CursorEffectRenderer renderer(*m_backend->cursorTimeline(), m_backend->cursorEffects());
    renderer.paint(*painter, m_position, {QRect(), videoSize, videoSize});
    painter->restore();
}
