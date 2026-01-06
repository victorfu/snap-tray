#ifndef DXGICAPTUREENGINE_H
#define DXGICAPTUREENGINE_H

#include "ICaptureEngine.h"

/**
 * @brief Windows capture engine using Desktop Duplication API
 *
 * Uses DXGI Desktop Duplication (DirectX 11.1+) for high-performance
 * hardware-accelerated screen capture on Windows 8+.
 *
 * Advantages:
 * - Hardware-accelerated capture
 * - Very low CPU overhead
 * - Captures protected content (when allowed)
 * - Best performance for high frame rate capture
 *
 * Requirements:
 * - Windows 8 or later
 * - DirectX 11.1 compatible GPU
 *
 * Falls back to BitBlt (GDI) on older systems or when Desktop
 * Duplication is unavailable (e.g., remote desktop sessions).
 */
class DXGICaptureEngine : public ICaptureEngine
{
    Q_OBJECT

public:
    explicit DXGICaptureEngine(QObject *parent = nullptr);
    ~DXGICaptureEngine() override;

    bool setRegion(const QRect &region, QScreen *screen) override;
    bool start() override;
    void stop() override;
    bool isRunning() const override;
    QImage captureFrame() override;
    QString engineName() const override;

    /**
     * @brief Check if DXGI Desktop Duplication is available
     * @return true if DirectX 11.1+ is supported and not in remote session
     */
    static bool isAvailable();

private:
    class Private;
    Private *d;
};

#endif // DXGICAPTUREENGINE_H
