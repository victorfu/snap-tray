#pragma once

#include "scrollcapture/FrameSettleDetector.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <algorithm>

namespace snaptray::scrollcapture::probe {

enum class SettleLoopResult { Finished, Cancelled, CaptureFailed };

template<typename Stop>
bool processCancellation(Stop stop)
{
    // Inspect guards measure/layer callbacks against reentrancy. Include input
    // events so native global shortcuts are delivered while capture is active.
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    return stop();
}

template<typename Stop>
void waitWithCancellation(int milliseconds, Stop stop)
{
    QElapsedTimer timer;
    timer.start();
    while (!processCancellation(stop) && timer.elapsed() < milliseconds) {
        constexpr int kPollMs = 50;
        const qint64 remaining = milliseconds - timer.elapsed();
        if (remaining <= 0) break;
        QThread::msleep(static_cast<unsigned long>(std::min<qint64>(kPollMs, remaining)));
    }
}

template<typename Capture, typename Elapsed, typename Stop>
SettleLoopResult settleFrames(FrameSettleDetector& detector, Capture capture, Elapsed elapsed, Stop stop)
{
    while (detector.result().outcome == FrameSettleDetector::Outcome::Pending) {
        if (processCancellation(stop)) return SettleLoopResult::Cancelled;
        const QImage frame = capture();
        if (processCancellation(stop)) return SettleLoopResult::Cancelled;
        if (frame.isNull()) return SettleLoopResult::CaptureFailed;
        detector.addFrame(frame, elapsed());
    }
    return SettleLoopResult::Finished;
}
}
