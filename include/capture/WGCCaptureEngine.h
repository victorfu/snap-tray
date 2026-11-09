#ifndef WGCCAPTUREENGINE_H
#define WGCCAPTUREENGINE_H

#include "ICaptureEngine.h"

#include <memory>

/**
 * @brief Windows capture engine using Windows Graphics Capture (#152)
 *
 * Captures a whole monitor through GraphicsCaptureItem::CreateForMonitor with
 * a free-threaded frame pool. Frames are event driven and carry the system's
 * capture time; while the screen is static the last frame is repeated at the
 * frame rate, so an unchanging screen does not look like a stalled capture.
 *
 * Capabilities are probed per property, not by OS version:
 * - IsCursorCaptureEnabled: the pointer is left out for the cursor sidecar;
 *   without the property it stays in the frames (capturesCursor() is true).
 * - IsBorderRequired: the capture border is turned off where the system
 *   allows it; borderRequired() reports what the system actually kept.
 * - MinUpdateInterval: limits delivery to the recording frame rate.
 *
 * Windows excluded with WDA_EXCLUDEFROMCAPTURE (SnapTray's controls) stay out
 * of the frames, as with Desktop Duplication. A closed capture item or a lost
 * device is reported through error(), never as a frozen but healthy stream.
 *
 * Experimental: selected by RecordingSettingsManager::windowsGraphicsCapture();
 * recording falls back to DXGICaptureEngine when it cannot start.
 */
class WGCCaptureEngine : public ICaptureEngine
{
    Q_OBJECT

public:
    explicit WGCCaptureEngine(QObject *parent = nullptr);
    ~WGCCaptureEngine() override;

    bool setRegion(const QRect &region, QScreen *screen) override;
    bool setRegion(const QRect &region, const CaptureScreenInfo &screenInfo) override;
    void setFrameRate(int fps) override;
    bool start() override;
    void stop() override;
    bool isRunning() const override;
    QImage captureFrame() override;
    bool deliversFrames() const override { return true; }
    bool capturesCursor() const override;
    std::shared_ptr<void> nativeFrameDevice() const override;
    void setNativeFrameOutput(bool enabled) override;
    QString engineName() const override;

    // Whether the system kept its capture border for this session.
    bool borderRequired() const;
    // Why the last start() failed; empty after a successful start.
    QString lastStartError() const;

    // GraphicsCaptureSession::IsSupported(); false on systems without WGC.
    static bool isAvailable();

private:
    class Private;
    std::shared_ptr<Private> d;
};

#endif // WGCCAPTUREENGINE_H
