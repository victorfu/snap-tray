#include "qml/RecordingKeyOverlay.h"

#include "qml/RecordingPreviewBackend.h"
#include "recording/KeyOverlay.h"

#include <QPainter>

RecordingKeyOverlay::RecordingKeyOverlay(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setFillColor(Qt::transparent);
}

QObject* RecordingKeyOverlay::backend() const
{
    return m_backend.data();
}

void RecordingKeyOverlay::setBackend(QObject* object)
{
    auto* backend = qobject_cast<RecordingPreviewBackend*>(object);
    if (m_backend == backend) return;
    if (m_backend) disconnect(m_backend, nullptr, this, nullptr);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &RecordingPreviewBackend::keyOverlayChanged, this, &RecordingKeyOverlay::invalidateRenderer);
        connect(m_backend, &RecordingPreviewBackend::timelineChanged, this, &RecordingKeyOverlay::invalidateRenderer);
        connect(m_backend, &RecordingPreviewBackend::trimRangeChanged, this, &RecordingKeyOverlay::invalidateRenderer);
        connect(m_backend, &RecordingPreviewBackend::durationChanged, this, &RecordingKeyOverlay::invalidateRenderer);
        connect(m_backend, &RecordingPreviewBackend::cropRectChanged, this, [this] { update(); });
    }
    m_renderer.reset();
    emit backendChanged();
    update();
}

void RecordingKeyOverlay::invalidateRenderer()
{
    m_renderer.reset();
    update();
}

const SnapTray::KeyOverlayRenderer* RecordingKeyOverlay::renderer()
{
    if (!m_renderer && m_backend && m_backend->keyTimeline()) {
        m_renderer = std::make_unique<SnapTray::KeyOverlayRenderer>(*m_backend->keyTimeline(), m_backend->keyOverlay());
        // The trimmed range always, like the export (PR #200 review).
        m_renderer->setTimeline(m_backend->timelineMap());
    }
    return m_renderer.get();
}

void RecordingKeyOverlay::setPosition(qint64 position)
{
    if (m_position == position) return;
    m_position = position;
    emit positionChanged();
    update();
}

void RecordingKeyOverlay::setContentRect(const QRectF& rect)
{
    if (m_contentRect == rect) return;
    m_contentRect = rect;
    emit contentRectChanged();
    update();
}

void RecordingKeyOverlay::paint(QPainter* painter)
{
    if (!m_backend || !m_backend->keyOverlayActive() || !m_backend->keyTimeline() || m_contentRect.isEmpty()) return;
    const QSize picture = m_backend->exportContentSize();
    if (picture.isEmpty()) return;
    if (const auto* keys = renderer()) keys->paint(*painter, m_position, m_contentRect, picture);
}
