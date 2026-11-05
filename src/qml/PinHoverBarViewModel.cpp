#include "qml/PinHoverBarViewModel.h"

#include <QtGlobal>

PinHoverBarViewModel::PinHoverBarViewModel(QObject* parent)
    : QObject(parent)
{
}

void PinHoverBarViewModel::setValues(qreal zoom, qreal minZoom, qreal opacity)
{
    const int zoomPercent = qRound(zoom * 100);
    const int minZoomPercent = qMax(1, qRound(minZoom * 100));
    const int opacityPercent = qRound(opacity * 100);
    if (zoomPercent == m_zoomPercent && minZoomPercent == m_minZoomPercent
        && opacityPercent == m_opacityPercent)
        return;
    m_zoomPercent = zoomPercent;
    m_minZoomPercent = minZoomPercent;
    m_opacityPercent = opacityPercent;
    emit valuesChanged();
}

void PinHoverBarViewModel::setStatus(Status status)
{
    if (m_status == status) return;
    m_status = status;
    emit statusChanged();
}

void PinHoverBarViewModel::setAvailableWidth(int width)
{
    const bool compact = width < kCompactWidth;
    if (compact == m_compact) return;
    m_compact = compact;
    emit compactChanged();
}

void PinHoverBarViewModel::openPanel(int panel)
{
    const int next = (panel == m_panel) ? int(NoPanel) : panel;
    if (next == m_panel) return;
    m_panel = next;
    emit panelChanged();
}

void PinHoverBarViewModel::closePanel()
{
    if (m_panel == NoPanel) return;
    m_panel = NoPanel;
    emit panelChanged();
}

void PinHoverBarViewModel::requestZoomPercent(int percent)
{
    emit zoomRequested(qBound(m_minZoomPercent, percent, kMaxZoomPercent) / 100.0);
}

void PinHoverBarViewModel::requestOpacityPercent(int percent)
{
    emit opacityRequested(qBound(kMinOpacityPercent, percent, kMaxOpacityPercent) / 100.0);
}

void PinHoverBarViewModel::toggleStatus()
{
    if (m_status == Live) emit pauseRequested();
    else if (m_status == Paused) emit resumeRequested();
}
