#ifndef QTCAPTUREENGINE_H
#define QTCAPTUREENGINE_H

#include "ICaptureEngine.h"

/**
 * @brief Cross-platform capture engine using Qt's screen grabbing
 *
 * Uses QScreen::grabWindow() for screen capture. This is the fallback
 * engine when platform-native capture is unavailable.
 *
 * Advantages:
 * - Works on all platforms Qt supports
 * - No special permissions required beyond standard Qt usage
 *
 * Disadvantages:
 * - Higher CPU usage than native APIs
 * - May not capture some overlay windows
 * - No hardware acceleration
 */
class QtCaptureEngine : public ICaptureEngine
{
    Q_OBJECT

public:
    explicit QtCaptureEngine(QObject *parent = nullptr);
    ~QtCaptureEngine() override;

    bool setRegion(const QRect &region, QScreen *screen) override;
    bool start() override;
    void stop() override;
    bool isRunning() const override;
    QImage captureFrame() override;
    QString engineName() const override { return QStringLiteral("Qt Screen Grab"); }

private:
    bool m_running = false;
};

#endif // QTCAPTUREENGINE_H
