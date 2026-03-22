#include "region/MagnifierOverlay.h"

#include "platform/WindowLevel.h"
#include "region/MagnifierPanel.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QShowEvent>

MagnifierOverlay::MagnifierOverlay(MagnifierPanel* panel, QWidget* parent)
    : QWidget(nullptr,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                  Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint)
    , m_panel(panel)
{
    Q_UNUSED(parent);

    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void MagnifierOverlay::syncToHost(QWidget* host,
                                  const QPoint& cursorPos,
                                  const QPixmap* backgroundPixmap,
                                  bool shouldShow)
{
    m_host = host;
    m_backgroundPixmap = backgroundPixmap;
    m_cursorPos = cursorPos;

    if (!m_panel || !m_host || !m_host->isVisible() ||
        !shouldShow || !m_backgroundPixmap || m_backgroundPixmap->isNull()) {
        hideOverlay();
        return;
    }

    const QRect globalRect = hostGlobalRect();
    const bool geometryChanged = geometry() != globalRect;
    if (geometryChanged) {
        setGeometry(globalRect);
    }

    const QRect currentMagnifierRect =
        m_dirtyRegionPlanner.magnifierRectForCursor(m_cursorPos, m_host->size());

    if (!isVisible()) {
        show();
        raise();
        // Clear stale back-buffer content from previous capture at old position.
        if (!m_lastMagnifierRect.isNull() && m_lastMagnifierRect != currentMagnifierRect) {
            update(m_lastMagnifierRect);
        }
        m_lastMagnifierRect = currentMagnifierRect;
        update(currentMagnifierRect);
        return;
    }

    if (geometryChanged || (!m_lastMagnifierRect.isNull() &&
                            m_lastMagnifierRect != currentMagnifierRect)) {
        update(m_lastMagnifierRect);
    }

    m_lastMagnifierRect = currentMagnifierRect;
    update(currentMagnifierRect);
    raise();
}

void MagnifierOverlay::hideOverlay()
{
    // Keep m_lastMagnifierRect so re-show can clear stale back-buffer content.
    hide();
}

void MagnifierOverlay::paintEvent(QPaintEvent* event)
{
    if (!m_panel || !m_backgroundPixmap || m_backgroundPixmap->isNull()) {
        return;
    }

    QPainter painter(this);
    painter.setClipRegion(event->region());
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(event->rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::Antialiasing);
    m_panel->draw(painter, m_cursorPos, size(), *m_backgroundPixmap);
}

void MagnifierOverlay::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    setWindowClickThrough(this, true);
    setWindowExcludedFromCapture(this, true);
    setWindowVisibleOnAllWorkspaces(this, true);
    raiseWindowAboveOverlays(this);
    raise();
}

QRect MagnifierOverlay::hostGlobalRect() const
{
    if (!m_host) {
        return QRect();
    }

    return QRect(m_host->mapToGlobal(QPoint(0, 0)), m_host->size());
}
