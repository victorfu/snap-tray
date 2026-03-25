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
    , m_beaverPixmap(QStringLiteral(":/icons/icons/cursor-beaver.png"))
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
                                  RegionCaptureSettingsManager::CursorCompanionStyle style,
                                  bool shouldShow)
{
    m_host = host;
    m_backgroundPixmap = backgroundPixmap;
    m_cursorPos = cursorPos;
    m_style = style;

    const bool canRenderMagnifier =
        m_panel && m_backgroundPixmap && !m_backgroundPixmap->isNull();
    const bool canRenderBeaver = !m_beaverPixmap.isNull();
    const bool canRenderCurrentStyle =
        (m_style == RegionCaptureSettingsManager::CursorCompanionStyle::Magnifier &&
         canRenderMagnifier) ||
        (m_style == RegionCaptureSettingsManager::CursorCompanionStyle::Beaver &&
         canRenderBeaver);

    if (!m_host || !m_host->isVisible() || !shouldShow || !canRenderCurrentStyle) {
        hideOverlay();
        return;
    }

    const QRect globalRect = hostGlobalRect();
    const bool geometryChanged = geometry() != globalRect;
    if (geometryChanged) {
        setGeometry(globalRect);
    }

    const QRect currentMagnifierRect =
        m_dirtyRegionPlanner.cursorCompanionRectForCursor(m_style, m_cursorPos, m_host->size());

    if (!isVisible()) {
        show();
        raise();
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
    m_lastMagnifierRect = QRect();
    hide();
}

void MagnifierOverlay::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setClipRegion(event->region());
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(event->rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (m_style == RegionCaptureSettingsManager::CursorCompanionStyle::Magnifier) {
        if (!m_panel || !m_backgroundPixmap || m_backgroundPixmap->isNull()) {
            return;
        }
        m_panel->draw(painter, m_cursorPos, size(), *m_backgroundPixmap);
        return;
    }

    if (m_style == RegionCaptureSettingsManager::CursorCompanionStyle::Beaver &&
        !m_beaverPixmap.isNull()) {
        drawBeaver(painter);
    }
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

void MagnifierOverlay::drawBeaver(QPainter& painter)
{
    const QRect iconRect =
        m_dirtyRegionPlanner.beaverRectForCursor(m_cursorPos, size());
    if (!iconRect.isValid()) {
        return;
    }

    painter.drawPixmap(iconRect, m_beaverPixmap);
}
