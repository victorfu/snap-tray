#ifndef SCROLLCAPTURESESSION_H
#define SCROLLCAPTURESESSION_H

#include "WindowDetector.h"
#include "scrollcapture/AutoScroller.h"
#include "scrollcapture/ContentRegionDetector.h"
#include "scrollcapture/FrameStabilityDetector.h"
#include "scrollcapture/ScrollCaptureTypes.h"
#include "scrollcapture/ScrollStitcher.h"

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QList>
#include <QtGui/qwindowdefs.h>

#include <atomic>
#include <memory>

class QScreen;
class ICaptureEngine;

class ScrollCaptureSession : public QObject
{
    Q_OBJECT

public:
    explicit ScrollCaptureSession(QObject* parent = nullptr);
    ~ScrollCaptureSession() override;

    void configure(const DetectedElement& element, ScrollCapturePreset preset);
    void setExcludedWindows(const QList<WId>& windowIds);
    void start();
    void stop();
    void cancel();
    bool isRunning() const { return m_running; }

signals:
    void stateChanged(ScrollCaptureState state);
    void progressChanged(int frameCount, int stitchedHeight, const QString& statusText);
    void warningRaised(const QString& warning);
    void finished(const ScrollCaptureResult& result);

private:
    bool initializeCaptureEngine(QString* errorMessage);
    void runCaptureLoop();
    bool captureWindowFrame(QImage* outFrame,
                            QString* errorMessage,
                            int waitTimeoutMs = 0) const;
    QRect logicalToImageRect(const QRect& logicalRect, const QImage& image) const;
    QImage cropContentFrame(const QImage& windowFrame) const;
    bool shouldStopForGuards(QString* reason) const;
    int computeStepPx(double multiplier) const;
    void finishWithError(const QString& reason);
    void finishWithResult(bool success, const QString& reason, const QString& warning = QString());

    DetectedElement m_element;
    ScrollCapturePreset m_preset = ScrollCapturePreset::WebPage;
    ScrollCaptureParams m_params;
    QList<WId> m_excludedWindowIds;

    QPointer<QScreen> m_targetScreen;
    QRect m_windowRect;
    QRect m_contentRect;

    std::unique_ptr<ICaptureEngine> m_captureEngine;
    AutoScroller m_autoScroller;
    ContentRegionDetector m_regionDetector;
    FrameStabilityDetector m_stabilityDetector;
    ScrollStitcher m_stitcher;

    std::atomic_bool m_stopRequested{false};
    std::atomic_bool m_cancelRequested{false};
    bool m_running = false;
    bool m_hasResult = false;

    int m_frameCount = 0;
    int m_smallDeltaStreak = 0;
    int m_noChangeStreak = 0;
    int m_noProgressStreak = 0;
    int m_weakMatchStreak = 0;
    bool m_forceWarpFallbackNextStep = false;
    int m_warpFallbackUsedCount = 0;
    double m_stepMultiplier = 1.0;
    QString m_lastWarning;
};

#endif // SCROLLCAPTURESESSION_H
