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
        connect(m_backend, &RecordingPreviewBackend::keyOverlayChanged, this, [this] { update(); });
        connect(m_backend, &RecordingPreviewBackend::timelineChanged, this, [this] { update(); });
        connect(m_backend, &RecordingPreviewBackend::cropRectChanged, this, [this] { update(); });
    }
    emit backendChanged();
    update();
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
    SnapTray::KeyOverlayRenderer renderer(*m_backend->keyTimeline(), m_backend->keyOverlay());
    if (m_backend->hasTimelineEdits()) renderer.setTimeline(m_backend->timelineMap());
    renderer.paint(*painter, m_position, m_contentRect, picture);
}
