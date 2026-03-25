#include "region/SelectionDimmingOverlay.h"

#include "region/CapturePerfRecorder.h"
#include "platform/WindowLevel.h"

#include <QPaintEvent>
#include <QPainter>
#include <QShowEvent>

namespace {
const QColor kSelectionDimmingColor(0, 0, 0, 100);
}

SelectionDimmingOverlay::SelectionDimmingOverlay(QWidget* parent)
    : QWidget(nullptr,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                  Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint)
{
    Q_UNUSED(parent);

    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void SelectionDimmingOverlay::syncToHost(QWidget* host, const QRect& selectionRect, bool shouldShow)
{
    snaptray::region::CapturePerfScope perfScope("SelectionDimmingOverlay.syncToHost");
    m_host = host;
    m_selectionRect = selectionRect.normalized();

    if (!m_host || !m_host->isVisible() || !shouldShow) {
        hideOverlay();
        return;
    }

    const QRect globalRect(m_host->mapToGlobal(QPoint(0, 0)), m_host->size());
    if (geometry() != globalRect) {
        setGeometry(globalRect);
    }

    QRegion maskRegion(rect());
    if (m_selectionRect.isValid() && !m_selectionRect.isEmpty()) {
        maskRegion = maskRegion.subtracted(QRegion(m_selectionRect));
    }
    setMask(maskRegion);

    if (!isVisible()) {
        show();
    }

    update();
}

void SelectionDimmingOverlay::hideOverlay()
{
    clearMask();
    hide();
}

void SelectionDimmingOverlay::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(event->rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.fillRect(event->rect(), kSelectionDimmingColor);
}

void SelectionDimmingOverlay::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    setWindowClickThrough(this, true);
    setWindowExcludedFromCapture(this, true);
    setWindowVisibleOnAllWorkspaces(this, true);
    raiseWindowAboveOverlays(this);
    raise();
}
