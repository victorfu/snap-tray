#include "qml/BeautifyPreviewItem.h"

#include "beautify/BeautifyRenderer.h"
#include "qml/BeautifyPanelBackend.h"

#include <QPainter>

BeautifyPreviewItem::BeautifyPreviewItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setOpaquePainting(false);
}

QObject* BeautifyPreviewItem::backend() const
{
    return m_backend;
}

void BeautifyPreviewItem::setBackend(QObject* backendObject)
{
    auto* typedBackend = qobject_cast<SnapTray::BeautifyPanelBackend*>(backendObject);
    if (m_backend == typedBackend) {
        return;
    }

    bindBackend(typedBackend);
    emit backendChanged();
    update();
}

void BeautifyPreviewItem::paint(QPainter* painter)
{
    if (!painter) {
        return;
    }

    painter->fillRect(boundingRect(), Qt::transparent);
    if (!m_backend || m_backend->sourcePixmap().isNull()) {
        return;
    }

    BeautifyRenderer::render(
        *painter,
        QRect(QPoint(0, 0), QSize(qMax(1, qRound(width())), qMax(1, qRound(height())))),
        m_backend->sourcePixmap(),
        m_backend->settings());
}

void BeautifyPreviewItem::bindBackend(SnapTray::BeautifyPanelBackend* backend)
{
    if (m_previewConnection) {
        disconnect(m_previewConnection);
    }
    if (m_settingsConnection) {
        disconnect(m_settingsConnection);
    }

    m_backend = backend;
    if (!m_backend) {
        return;
    }

    m_previewConnection = connect(
        m_backend,
        &SnapTray::BeautifyPanelBackend::previewRevisionChanged,
        this,
        [this]() { update(); });
    m_settingsConnection = connect(
        m_backend,
        &SnapTray::BeautifyPanelBackend::settingsStateChanged,
        this,
        [this]() { update(); });
}
