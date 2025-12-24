#ifndef CAPTUREMANAGER_H
#define CAPTUREMANAGER_H

#include <QObject>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QPointer>

class RegionSelector;
class QScreen;
class PinWindowManager;
class WindowDetector;

class CaptureManager : public QObject
{
    Q_OBJECT

public:
    explicit CaptureManager(PinWindowManager *pinManager, QObject *parent = nullptr);
    ~CaptureManager();

    bool isActive() const;

public slots:
    void startRegionCapture();
    void startRegionCaptureWithPreset(const QRect &region, QScreen *screen);

signals:
    void captureStarted();
    void captureCompleted(const QPixmap &screenshot);
    void captureCancelled();
    void recordingRequested(const QRect &region, QScreen *screen);

private slots:
    void onRegionSelected(const QPixmap &screenshot, const QPoint &globalPosition);
    void onSelectionCancelled();

private:
    QPointer<RegionSelector> m_regionSelector;
    PinWindowManager *m_pinManager;
    WindowDetector *m_windowDetector;
};

#endif // CAPTUREMANAGER_H
