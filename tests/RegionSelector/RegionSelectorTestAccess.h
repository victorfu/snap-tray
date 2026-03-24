#pragma once

#include <QPaintEvent>
#include <QRegion>

#include "RegionSelector.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"
#include "qml/QmlOverlayPanel.h"
#include "region/RegionInputHandler.h"
#include "region/MagnifierOverlay.h"
#include "region/SelectionStateManager.h"

class RegionSelectorTestAccess
{
public:
    using TraceProbe = RegionSelector::RegionSelectorTraceProbe;

    static void attachTraceProbe(
        RegionSelector& selector,
        TraceProbe* probe)
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

    static void markInitialRevealRevealed(RegionSelector& selector)
    {
        selector.m_initialRevealState = RegionSelector::InitialRevealState::Revealed;
    }

    static void markInitialRevealPreparing(RegionSelector& selector)
    {
        selector.m_initialRevealState = RegionSelector::InitialRevealState::Preparing;
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

    static bool initialRevealStateIsRevealed(const RegionSelector& selector)
    {
        return selector.m_initialRevealState == RegionSelector::InitialRevealState::Revealed;
    }

    static void showForRevealTests(RegionSelector& selector)
    {
        selector.show();
    }

    static int captureContextEventCount(const TraceProbe& probe)
    {
        return probe.captureContextEvents.size();
    }

    static SnapTray::CaptureSessionWriteRequest buildCaptureSessionWriteRequest(
        RegionSelector& selector,
        const QPixmap& backgroundPixmap,
        const QImage& resultImage,
        const QRect& selectionRect,
        const QVector<MultiRegionManager::Region>& captureRegions,
        const QByteArray& annotationsJson,
        qreal devicePixelRatio,
        const QSize& canvasLogicalSize,
        int cornerRadius,
        int maxEntries,
        const QDateTime& createdAt)
    {
        RegionSelector::PendingHistorySubmission submission;
        submission.snapshot.backgroundPixmap = backgroundPixmap;
        submission.snapshot.selectionRect = selectionRect;
        submission.snapshot.captureRegions = captureRegions;
        submission.snapshot.annotationsJson = annotationsJson;
        submission.snapshot.devicePixelRatio = devicePixelRatio;
        submission.snapshot.canvasLogicalSize = canvasLogicalSize;
        submission.snapshot.cornerRadius = cornerRadius;
        submission.snapshot.maxEntries = maxEntries;
        submission.snapshot.createdAt = createdAt;
        submission.resultImage = resultImage;
        return selector.buildCaptureSessionWriteRequest(submission);
    }

    static void setHistoryLiveSlot(RegionSelector& selector,
                                   const QPixmap& backgroundPixmap,
                                   qreal devicePixelRatio,
                                   const QSize& canvasLogicalSize,
                                   const QRect& selectionRect,
                                   int cornerRadius)
    {
        selector.m_historyLiveSlot.valid = true;
        selector.m_historyLiveSlot.canvasLogicalSize = canvasLogicalSize;
        selector.m_historyLiveSlot.backgroundPixmap = backgroundPixmap;
        selector.m_historyLiveSlot.devicePixelRatio = devicePixelRatio;
        selector.m_historyLiveSlot.selectionRect = selectionRect;
        selector.m_historyLiveSlot.cornerRadius = cornerRadius;
    }

    static void invokeRestoreLiveReplaySlot(RegionSelector& selector)
    {
        selector.restoreLiveReplaySlot();
    }

    static bool invokeApplyHistoryReplayEntry(
        RegionSelector& selector,
        const SnapTray::HistoryEntry& entry)
    {
        return selector.applyHistoryReplayEntry(entry);
    }

    static QRect selectionRect(const RegionSelector& selector)
    {
        return selector.m_selectionManager->selectionRect();
    }

    static QSize backgroundPixelSize(const RegionSelector& selector)
    {
        return selector.m_backgroundPixmap.size();
    }

    static qreal devicePixelRatio(const RegionSelector& selector)
    {
        return selector.m_devicePixelRatio;
    }

    static bool toolbarVisible(const RegionSelector& selector)
    {
        return selector.m_qmlToolbar && selector.m_qmlToolbar->isVisible();
    }

    static QRect toolbarGeometry(const RegionSelector& selector)
    {
        return selector.m_qmlToolbar ? selector.m_qmlToolbar->geometry() : QRect();
    }

    static bool subToolbarVisible(const RegionSelector& selector)
    {
        return selector.m_qmlSubToolbar && selector.m_qmlSubToolbar->isVisible();
    }

    static bool regionControlVisible(const RegionSelector& selector)
    {
        return selector.m_regionControlPanel && selector.m_regionControlPanel->isVisible();
    }

    static QRect regionControlGeometry(const RegionSelector& selector)
    {
        return selector.m_regionControlPanel ? selector.m_regionControlPanel->geometry() : QRect();
    }

    static bool magnifierVisible(const RegionSelector& selector)
    {
        return selector.m_magnifierOverlay && selector.m_magnifierOverlay->isVisible();
    }

    static QRect magnifierGeometry(const RegionSelector& selector)
    {
        return selector.m_magnifierOverlay ? selector.m_magnifierOverlay->geometry() : QRect();
    }
};
