#ifndef SCKCAPTUREENGINE_H
#define SCKCAPTUREENGINE_H

#include "ICaptureEngine.h"

/**
 * @brief macOS capture engine using ScreenCaptureKit
 *
 * Uses Apple's ScreenCaptureKit framework (macOS 12.3+) for high-performance
 * screen capture with hardware acceleration.
 *
 * Advantages:
 * - Hardware-accelerated capture
 * - Lower CPU/power usage than Qt fallback
 * - Better color accuracy
 * - Can capture protected content when permitted
 *
 * Requirements:
 * - macOS 12.3 or later
 * - Screen recording permission
 *
 * Falls back to CGWindowListCreateImage on older macOS versions.
 */
class SCKCaptureEngine : public ICaptureEngine
{
    Q_OBJECT

public:
    explicit SCKCaptureEngine(QObject *parent = nullptr);
    ~SCKCaptureEngine() override;

    bool setRegion(const QRect &region, QScreen *screen) override;
    void setFrameRate(int fps) override;
    bool start() override;
    void stop() override;
    bool isRunning() const override;
    QImage captureFrame() override;
    QString engineName() const override;

    /**
     * @brief Check if ScreenCaptureKit is available on this system
     * @return true if macOS 12.3+ and framework is present
     */
    static bool isAvailable();

    /**
     * @brief Check if the app has screen recording permission
     * @return true if permission granted
     */
    static bool hasScreenRecordingPermission();

public:
    // Private implementation class - public for Objective-C delegate access
    class Private;

private:
    Private *d;
};

#endif // SCKCAPTUREENGINE_H
