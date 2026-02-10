#ifndef SCROLLCAPTUREMANAGER_H
#define SCROLLCAPTUREMANAGER_H

#include "WindowDetector.h"
#include "scrollcapture/ScrollCaptureTypes.h"

#include <QObject>
#include <QPointer>

#include <memory>

class PinWindowManager;
class RegionSelector;
class ScrollCaptureControlBar;
class ScrollCaptureSession;
class WindowDetector;

class ScrollCaptureManager : public QObject
{
    Q_OBJECT

public:
    explicit ScrollCaptureManager(PinWindowManager* pinManager, QObject* parent = nullptr);
    ~ScrollCaptureManager() override;

    bool isActive() const;

public slots:
    void beginWindowPick();
    void stop();
    void cancel();

signals:
    void captureStarted();
    void captureCompleted(bool success);

private slots:
    void onWindowChosen(const DetectedElement& element);
    void onPickerCancelled();
    void onPresetWebRequested();
    void onPresetChatRequested();
    void onStopRequested();
    void onCancelRequested();
    void onOpenSettingsRequested();
    void onSessionStateChanged(ScrollCaptureState state);
    void onSessionProgress(int frameCount, int stitchedHeight, const QString& statusText);
    void onSessionWarning(const QString& warning);
    void onSessionFinished(const ScrollCaptureResult& result);

private:
    void cleanupPicker();
    void cleanupSession();
    void startSession(ScrollCapturePreset preset);
    QPoint centerPositionForPixmap(const QPixmap& pixmap) const;

    PinWindowManager* m_pinManager = nullptr;
    QPointer<RegionSelector> m_windowPicker;
    QPointer<ScrollCaptureControlBar> m_controlBar;
    std::unique_ptr<WindowDetector> m_windowDetector;
    std::unique_ptr<ScrollCaptureSession> m_session;

    bool m_active = false;
    bool m_waitingPreset = false;
    DetectedElement m_selectedWindow;
};

#endif // SCROLLCAPTUREMANAGER_H
