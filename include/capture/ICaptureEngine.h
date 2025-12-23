#ifndef ICAPTUREENGINE_H
#define ICAPTUREENGINE_H

#include <QObject>
#include <QRect>
#include <QImage>

class QScreen;

/**
 * @brief Abstract interface for screen capture engines
 *
 * Provides a platform-agnostic interface for capturing screen regions.
 * Implementations include:
 * - QtCaptureEngine: Cross-platform fallback using QScreen::grabWindow()
 * - SCKCaptureEngine: macOS ScreenCaptureKit (12.3+)
 * - DXGICaptureEngine: Windows Desktop Duplication API
 */
class ICaptureEngine : public QObject
{
    Q_OBJECT

public:
    explicit ICaptureEngine(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~ICaptureEngine() = default;

    /**
     * @brief Configure the capture region
     * @param region Screen region to capture (global coordinates)
     * @param screen Target screen containing the region
     * @return true if configuration successful
     */
    virtual bool setRegion(const QRect &region, QScreen *screen) = 0;

    /**
     * @brief Set the target frame rate
     * @param fps Frames per second (used by engines with async delivery)
     */
    virtual void setFrameRate(int fps) { m_frameRate = fps; }

    /**
     * @brief Initialize and start the capture engine
     * @return true if started successfully
     */
    virtual bool start() = 0;

    /**
     * @brief Stop the capture engine and release resources
     */
    virtual void stop() = 0;

    /**
     * @brief Check if the engine is currently running
     */
    virtual bool isRunning() const = 0;

    /**
     * @brief Capture a single frame synchronously
     * @return Captured frame, or null QImage on failure
     */
    virtual QImage captureFrame() = 0;

    /**
     * @brief Get the name of this capture engine
     */
    virtual QString engineName() const = 0;

    /**
     * @brief Create the best available capture engine for the current platform
     * @param parent Parent QObject for ownership
     * @return New capture engine instance (caller takes ownership)
     */
    static ICaptureEngine *createBestEngine(QObject *parent = nullptr);

    /**
     * @brief Check if a native (non-Qt) capture engine is available
     */
    static bool isNativeEngineAvailable();

signals:
    /**
     * @brief Emitted when a frame is captured asynchronously
     * @param frame The captured frame
     * @param timestamp Capture timestamp in milliseconds
     */
    void frameReady(const QImage &frame, qint64 timestamp);

    /**
     * @brief Emitted when a capture error occurs
     * @param message Error description
     */
    void error(const QString &message);

    /**
     * @brief Emitted when a non-fatal warning occurs
     * @param message Warning description
     */
    void warning(const QString &message);

protected:
    QRect m_captureRegion;
    QScreen *m_targetScreen = nullptr;
    int m_frameRate = 30;
};

#endif // ICAPTUREENGINE_H
