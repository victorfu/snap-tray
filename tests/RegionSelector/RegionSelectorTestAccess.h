#pragma once

#include <QCoreApplication>
#include <QEnterEvent>
#include <QPaintEvent>
#include <QRegion>

#include "RegionSelector.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"
#include "qml/QmlOverlayPanel.h"
#include "region/CaptureChromeWindow.h"
#include "region/RegionInputHandler.h"
#include "region/MagnifierOverlay.h"
#include "region/SelectionDimmingOverlay.h"
#include "region/SelectionPreviewOverlay.h"
#include "region/SelectionStateManager.h"
#include "region/StaticCaptureBackgroundWindow.h"

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

    static void dispatchWidgetMouseMove(RegionSelector& selector,
                                        const QPoint& localPos,
                                        Qt::MouseButtons buttons = Qt::NoButton)
    {
        const QPoint globalPos = selector.mapToGlobal(localPos);
        QMouseEvent moveEvent(QEvent::MouseMove,
                              QPointF(localPos),
                              QPointF(localPos),
                              QPointF(globalPos),
                              Qt::NoButton,
                              buttons,
                              Qt::NoModifier);
        selector.mouseMoveEvent(&moveEvent);
    }

    static void dispatchWindowEnter(QWindow* window, const QPoint& globalPos)
    {
        if (!window) {
            return;
        }

        const QPoint localPos = window->mapFromGlobal(globalPos);
        const QPointF localPosF(localPos);
        const QPointF globalPosF(globalPos);
        QEnterEvent enterEvent{localPosF, localPosF, globalPosF};
        QCoreApplication::sendEvent(window, &enterEvent);
    }

    static void dispatchMousePress(RegionSelector& selector,
                                   const QPoint& localPos,
                                   Qt::MouseButton button = Qt::LeftButton,
                                   Qt::MouseButtons buttons = Qt::LeftButton)
    {
        QMouseEvent pressEvent(QEvent::MouseButtonPress,
                               QPointF(localPos),
                               QPointF(localPos),
                               QPointF(localPos),
                               button,
                               buttons,
                               Qt::NoModifier);
        selector.m_inputHandler->handleMousePress(&pressEvent);
    }

    static void dispatchMouseRelease(RegionSelector& selector,
                                     const QPoint& localPos,
                                     Qt::MouseButton button = Qt::LeftButton,
                                     Qt::MouseButtons buttons = Qt::NoButton)
    {
        QMouseEvent releaseEvent(QEvent::MouseButtonRelease,
                                 QPointF(localPos),
                                 QPointF(localPos),
                                 QPointF(localPos),
                                 button,
                                 buttons,
                                 Qt::NoModifier);
        selector.m_inputHandler->handleMouseRelease(&releaseEvent);
    }

    static void seedDetectedWindowHighlight(RegionSelector& selector, const QRect& rect)
    {
        selector.m_inputState.highlightedWindowRect = rect;
        selector.m_inputState.hasDetectedWindow = rect.isValid() && !rect.isEmpty();
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

    static QWindow* toolbarWindow(const RegionSelector& selector)
    {
        return selector.m_qmlToolbar ? selector.m_qmlToolbar->window() : nullptr;
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

    static bool selectionPreviewVisible(const RegionSelector& selector)
    {
        return selector.m_selectionPreviewOverlay && selector.m_selectionPreviewOverlay->isVisible();
    }

    static bool selectionDimmingVisible(const RegionSelector& selector)
    {
        return selector.m_selectionDimmingOverlay && selector.m_selectionDimmingOverlay->isVisible();
    }

    static bool usesDetachedCaptureWindows(const RegionSelector& selector)
    {
        return selector.usesDetachedCaptureWindows();
    }

    static bool captureChromeVisible(const RegionSelector& selector)
    {
        return selector.m_captureChromeWindow && selector.m_captureChromeWindow->isVisible();
    }

    static bool selectionCompletionHandoffPending(const RegionSelector& selector)
    {
        return selector.m_selectionCompletionHandoffPending;
    }

    static bool staticCaptureBackgroundVisible(const RegionSelector& selector)
    {
        return selector.m_staticCaptureBackgroundWindow &&
               selector.m_staticCaptureBackgroundWindow->isVisible();
    }

    static CaptureChromeWindow* captureChromeWindow(RegionSelector& selector)
    {
        return selector.m_captureChromeWindow.get();
    }
};
