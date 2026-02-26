#ifndef SCROLLCAPTURESESSION_H
#define SCROLLCAPTURESESSION_H

#include <QObject>
#include <QList>
#include <QPointer>
#include <QPixmap>
#include <QImage>
#include <QRect>
#include <QElapsedTimer>
#include <qwindowdefs.h>

#include "platform/IScrollAutomationDriver.h"
#include "scroll/ScrollStitchEngine.h"

class QTimer;
class QWidget;
class QLabel;
class QPushButton;
class QScreen;
class ICaptureEngine;
class ScrollCaptureControlBar;

class ScrollCaptureSession : public QObject
{
    Q_OBJECT

public:
    struct Dependencies {
        ICaptureEngine *captureEngine;                 // Optional injection for tests.
        IScrollAutomationDriver *automationDriver;     // Optional injection for tests.
        bool enableUi;                                 // Disable UI in tests.

        explicit Dependencies(ICaptureEngine *captureEngine = nullptr,
                              IScrollAutomationDriver *automationDriver = nullptr,
                              bool enableUi = true)
            : captureEngine(captureEngine)
            , automationDriver(automationDriver)
            , enableUi(enableUi)
        {
        }
    };

    enum class Mode {
        Auto,
        Hybrid,
        Manual
    };

    enum class AutoScrollPhase {
        Idle,
        ReadyToScroll,
        ScrollIssued,
        MovementObserved,
        WaitingForSettle
    };

    explicit ScrollCaptureSession(const QRect &region,
                                  QScreen *screen,
                                  QObject *parent = nullptr,
                                  const Dependencies &deps = Dependencies());
    ~ScrollCaptureSession() override;

    void start();
    void startAutoAssist();
    void cancel();
    void finish();
    void pause();
    void resume();
    void interruptAuto();

    bool isRunning() const { return m_running; }
    bool isPaused() const { return m_paused; }
    Mode mode() const { return m_mode; }

signals:
    void captureReady(const QPixmap &screenshot, const QPoint &globalPosition, const QRect &globalRect);
    void cancelled();
    void failed(const QString &error);

private:
    void switchToHybridMode(const QString &reason = QString());
    void switchToManualMode(const QString &reason = QString());
    QList<QPoint> buildProbePoints() const;
    bool tryProbeAutoMode(int maxProbeCount = -1);
    void scheduleAutoProbe();
    void processAutoProbeStep();
    QList<WId> collectExcludedWindowIds() const;
    void shutdownRuntime();
    void setupUi();
    void cleanupUi();
    void updatePreview(bool force = false);
    void updatePreviewMetaLabel();
    void updateStatusLabel();
    void updateProgressLabel();
    void updateQualityLabel(StitchQuality quality, double confidence);
    bool isAutoDegraded() const;
    void onWatchdogTick();
    void onCaptureTick();

    QRect m_region;
    QPointer<QScreen> m_screen;

    Dependencies m_deps;
    ICaptureEngine *m_captureEngine = nullptr;
    IScrollAutomationDriver *m_scrollDriver = nullptr;
    bool m_ownsCaptureEngine = false;
    bool m_ownsScrollDriver = false;

    QTimer *m_captureTimer = nullptr;
    QTimer *m_watchdogTimer = nullptr;
    ScrollStitchEngine m_stitchEngine;
    bool m_stitchStarted = false;
    bool m_running = false;
    bool m_paused = false;
    Mode m_mode = Mode::Manual;
    int m_noChangeCount = 0;
    int m_initialCombineAttempts = 0;
    int m_initialBadCount = 0;
    int m_initialFrameMisses = 0;
    bool m_syntheticAutoFallback = false;
    bool m_hasConfirmedMotion = false;
    bool m_probeInFlight = false;
    int m_autoNoMotionStepCount = 0;
    QPoint m_probeAnchor;
    bool m_hasProbeAnchor = false;
    bool m_recoveryFocusTried = false;
    bool m_recoverySyntheticTried = false;
    bool m_focusRecommended = false;
    bool m_interruptAutoPending = false;
    bool m_captureTimerRestarted = false;
    int m_consecutiveUnlockedAutoSteps = 0;
    int m_probeFailureCount = 0;
    int m_autoStepDelayMs = 140;
    int m_settleStableFramesRequired = 2;
    qint64 m_settleTimeoutMs = 220;
    int m_probeGridDensity = 3;
    QList<QPoint> m_pendingProbePoints;
    int m_probeIndex = 0;
    int m_probeMaxPoints = 0;
    bool m_probeExpandedPass = false;
    QString m_autoDriverStatus = QStringLiteral("AX");
    QString m_manualReason;

    QElapsedTimer m_runtimeClock;
    qint64 m_startedElapsedMs = 0;
    qint64 m_lastTickElapsedMs = 0;
    qint64 m_lastFrameElapsedMs = -1;
    qint64 m_lastAutoStepElapsedMs = -1;
    QImage m_preStepFrame;
    QImage m_lastObservedFrame;
    AutoScrollPhase m_autoPhase = AutoScrollPhase::Idle;
    int m_settleStableFrameCount = 0;
    qint64 m_autoPhaseEnteredAtMs = 0;
    qint64 m_lastMovementObservedAtMs = -1;
    bool m_previewDirty = true;
    qint64 m_lastPreviewUpdateElapsedMs = -1;
    QString m_lastStatusLine;
    QString m_progressLine;
    StitchQuality m_lastQuality = StitchQuality::NoChange;
    double m_lastQualityConfidence = 0.0;
    StitchMatchStrategy m_lastStrategy = StitchMatchStrategy::RowDiff;

    ScrollCaptureControlBar *m_controlBar = nullptr;
    QWidget *m_boundaryOverlay = nullptr;
};

#endif // SCROLLCAPTURESESSION_H
