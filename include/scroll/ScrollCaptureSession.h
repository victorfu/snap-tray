#ifndef SCROLLCAPTURESESSION_H
#define SCROLLCAPTURESESSION_H

#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QVector>
#include <qwindowdefs.h>

#include "scroll/ScrollStitchEngine.h"

class QScreen;
class QTimer;
class QWidget;
class ICaptureEngine;
template <typename T> class QFutureWatcher;
namespace SnapTray {
class QmlRecordingBoundary;
class QmlScrollControlBar;
class QmlScrollPreviewWindow;
}

class ScrollCaptureSession : public QObject
{
    Q_OBJECT

public:
    struct Dependencies {
        ICaptureEngine *captureEngine; // Optional injection for tests.
        bool enableUi;                 // Disable UI in tests.

        explicit Dependencies(ICaptureEngine *captureEngine = nullptr,
                              bool enableUi = true)
            : captureEngine(captureEngine)
            , enableUi(enableUi)
        {
        }
    };

    enum class Mode {
        Manual
    };

    explicit ScrollCaptureSession(const QRect &region,
                                  QScreen *screen,
                                  QObject *parent = nullptr,
                                  const Dependencies &deps = Dependencies());
    ~ScrollCaptureSession() override;

    void start();
    void cancel();
    void finish();
    void pause();
    void resume();
    void exportTraceSnapshot(const QString &reason);

    bool isRunning() const { return m_running; }
    bool isPaused() const { return m_paused; }
    Mode mode() const { return Mode::Manual; }

signals:
    void captureReady(const QPixmap &screenshot, const QPoint &globalPosition, const QRect &globalRect);
    void cancelled();
    void failed(const QString &error);

private:
    struct ScrollTraceEntry {
        qint64 elapsedMs = 0;
        QString event;
        StitchQuality quality = StitchQuality::NoChange;
        StitchRejectionCode rejectionCode = StitchRejectionCode::None;
        int appendHeight = 0;
        double confidence = 0.0;
        double rawNccScore = 0.0;
        double confidencePenalty = 0.0;
        double appendPlausibilityScore = 0.0;
        bool duplicateLoopDetected = false;
        bool blockedByLowConfidence = false;
        qint64 previewUpdateMs = -1;
    };

    QList<WId> collectExcludedWindowIds() const;
    void shutdownRuntime();
    void setupUi();
    void cleanupUi();
    void updatePreview(bool force = false);
    void updateStatusLabel();
    void updateProgressLabel();
    void updateQualityLabel(StitchQuality quality, double confidence);
    void updateSlowScrollWarningUi();
    bool handleAppendResult(const StitchFrameResult &appendResult);
    void queueAppendFrame(const QImage &frame);
    void dispatchNextAppendIfIdle();
    void onAppendTaskFinished();
    void drainAppendBeforeFinish();
    void pushTraceEntry(const ScrollTraceEntry &entry);
    void traceEvent(const QString &event);
    void traceAppendResult(const StitchFrameResult &result, qint64 previewUpdateMs);
    void onWatchdogTick();
    void onCaptureTick();

    QRect m_region;
    QPointer<QScreen> m_screen;

    Dependencies m_deps;
    ICaptureEngine *m_captureEngine = nullptr;
    bool m_ownsCaptureEngine = false;

    QTimer *m_captureTimer = nullptr;
    QTimer *m_watchdogTimer = nullptr;

    ScrollStitchEngine m_stitchEngine;
    bool m_stitchStarted = false;
    bool m_running = false;
    bool m_finishing = false;
    bool m_paused = false;

    int m_initialCombineAttempts = 0;
    int m_initialBadCount = 0;
    int m_initialFrameMisses = 0;

    QElapsedTimer m_runtimeClock;
    qint64 m_startedElapsedMs = 0;
    qint64 m_lastTickElapsedMs = 0;
    qint64 m_lastFrameElapsedMs = -1;

    bool m_previewDirty = true;
    bool m_stitchedPreviewDirty = true;
    qint64 m_lastPreviewUpdateElapsedMs = -1;
    qint64 m_lastStitchedPreviewUpdateElapsedMs = -1;

    StitchQuality m_lastQuality = StitchQuality::NoChange;
    double m_lastQualityConfidence = 0.0;
    StitchMatchStrategy m_lastStrategy = StitchMatchStrategy::RowDiff;

    StitchPreviewMeta m_cachedPreviewMeta;
    int m_cachedFrameCount = 0;
    int m_cachedTotalHeight = 0;

    QString m_lastStatusLine;
    QString m_progressLine;
    QString m_manualReason;

    int m_consecutiveLowConfidenceCount = 0;
    bool m_blockedByLowConfidence = false;
    bool m_slowScrollWarningActive = false;

    QMutex m_stitchMutex;
    QFutureWatcher<StitchFrameResult> *m_appendWatcher = nullptr;
    bool m_appendInFlight = false;
    int m_appendInFlightGeneration = 0;
    int m_appendGeneration = 0;
    QImage m_pendingAppendFrame;
    bool m_hasPendingAppendFrame = false;
    int m_pendingExternalDyHint = 0;

    QVector<ScrollTraceEntry> m_sessionTrace;

    SnapTray::QmlScrollControlBar *m_controlBar = nullptr;
    SnapTray::QmlScrollPreviewWindow *m_previewWindow = nullptr;
    QPointer<SnapTray::QmlRecordingBoundary> m_boundaryOverlay;
};

#endif // SCROLLCAPTURESESSION_H
