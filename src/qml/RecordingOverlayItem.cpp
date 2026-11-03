#include "qml/RecordingOverlayItem.h"

#include "qml/RecordingPreviewBackend.h"
#include "recording/OverlayTrack.h"

#include <QPainter>

RecordingOverlayItem::RecordingOverlayItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setFillColor(Qt::transparent);
}

QObject* RecordingOverlayItem::backend() const
{
    return m_backend.data();
}

void RecordingOverlayItem::setBackend(QObject* object)
{
    auto* backend = qobject_cast<RecordingPreviewBackend*>(object);
    if (m_backend == backend) return;
    if (m_backend) disconnect(m_backend, nullptr, this, nullptr);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &RecordingPreviewBackend::overlaysChanged, this, [this] { update(); });
        connect(m_backend, &RecordingPreviewBackend::cropRectChanged, this, [this] { update(); });
    }
    emit backendChanged();
    update();
}

void RecordingOverlayItem::setPosition(qint64 position)
{
    if (m_position == position) return;
    m_position = position;
    emit positionChanged();
    update();
}

void RecordingOverlayItem::setContentRect(const QRectF& rect)
{
    if (m_contentRect == rect) return;
    m_contentRect = rect;
    emit contentRectChanged();
    update();
}

void RecordingOverlayItem::paint(QPainter* painter)
{
    if (!m_backend || !m_backend->overlaysAvailable() || m_backend->overlayTrack().isEmpty() || m_contentRect.isEmpty()) {
        return;
    }
    const QSize picture = m_backend->exportContentSize();
    if (picture.isEmpty()) return;
    m_backend->overlayTrack().paint(*painter, m_position, m_contentRect, picture);
}
