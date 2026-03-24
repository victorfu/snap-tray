#pragma once

#include <QPaintEvent>
#include <QRegion>

#include "RegionSelector.h"
#include "region/RegionInputHandler.h"

class RegionSelectorTestAccess
{
public:
    static void attachTraceProbe(
        RegionSelector& selector,
        RegionSelector::RegionSelectorTraceProbe* probe)
    {
        selector.m_traceProbe = probe;
    }

    static void dispatchMouseMove(RegionSelector& selector, const QPoint& localPos)
    {
        QMouseEvent moveEvent(QEvent::MouseMove,
                              QPointF(localPos),
                              QPointF(localPos),
                              Qt::NoButton,
                              Qt::NoButton,
                              Qt::NoModifier);
        selector.m_inputHandler->handleMouseMove(&moveEvent);
    }

    static void setSelectionRect(RegionSelector& selector, const QRect& rect)
    {
        selector.m_selectionManager->setSelectionRect(rect);
    }

    static void setInitialRevealState(
        RegionSelector& selector,
        RegionSelector::InitialRevealState state)
    {
        selector.m_initialRevealState = state;
    }

    static RegionSelector::InitialRevealState initialRevealState(const RegionSelector& selector)
    {
        return selector.m_initialRevealState;
    }

    static void invokePaint(RegionSelector& selector, const QRegion& dirtyRegion)
    {
        QPaintEvent event(dirtyRegion);
        selector.paintEvent(&event);
    }

    static void invokeHandleInitialRevealTimeout(RegionSelector& selector)
    {
        selector.handleInitialRevealTimeout();
    }

    static void showForRevealTests(RegionSelector& selector)
    {
        selector.show();
    }

    static int captureContextEventCount(
        const RegionSelector::RegionSelectorTraceProbe& probe)
    {
        return probe.captureContextEvents.size();
    }
};
