#include "qml/RecordingAppearanceOverlay.h"

#include "qml/RecordingPreviewBackend.h"

#include <QPainter>

RecordingAppearanceOverlay::RecordingAppearanceOverlay(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setFillColor(Qt::transparent);
}

QObject* RecordingAppearanceOverlay::backend() const
{
    return m_backend.data();
}

void RecordingAppearanceOverlay::setBackend(QObject* object)
{
    auto* backend = qobject_cast<RecordingPreviewBackend*>(object);
    if (m_backend == backend) return;
    if (m_backend) disconnect(m_backend, nullptr, this, nullptr);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &RecordingPreviewBackend::appearanceChanged, this, &RecordingAppearanceOverlay::invalidate);
        connect(m_backend, &RecordingPreviewBackend::cropRectChanged, this, &RecordingAppearanceOverlay::invalidate);
        connect(m_backend, &RecordingPreviewBackend::videoSizeChanged, this, &RecordingAppearanceOverlay::invalidate);
        connect(m_backend, &RecordingPreviewBackend::appearancePreviewFrameChanged, this, [this] { update(); });
    }
    emit backendChanged();
    invalidate();
}

void RecordingAppearanceOverlay::setContentRect(const QRectF& rect)
{
    if (m_contentRect == rect) return;
    m_contentRect = rect;
    emit contentRectChanged();
    update();
}

void RecordingAppearanceOverlay::invalidate()
{
    m_renderer.reset();
    update();
}

void RecordingAppearanceOverlay::paint(QPainter* painter)
{
    if (!m_backend || !m_backend->appearanceActive() || m_contentRect.isEmpty()) return;
    const QSize content = m_backend->exportContentSize();
    if (content.isEmpty()) return;
    const SnapTray::RecordingAppearance& appearance = m_backend->appearance();
    if (!m_renderer || m_rendererAppearance != appearance || m_rendererSize != content) {
        if (appearance.imagePath != m_rendererAppearance.imagePath || m_backgroundImage.isNull()) {
            m_backgroundImage = appearance.background == SnapTray::RecordingAppearance::Background::Image
                ? QImage(appearance.imagePath) : QImage();
        }
        m_renderer = std::make_unique<SnapTray::RecordingAppearanceRenderer>(appearance, content, m_backgroundImage,
                                                                            false);
        m_rendererAppearance = appearance;
        m_rendererSize = content;
    }
    m_renderer->paintAround(*painter, m_contentRect, m_backend->appearancePreviewFrame());
}
