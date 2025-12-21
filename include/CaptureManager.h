#ifndef CAPTUREMANAGER_H
#define CAPTUREMANAGER_H

#include <QObject>
#include <QPixmap>
#include <QPoint>
#include <QPointer>

class RegionSelector;
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

signals:
    void captureStarted();
    void captureCompleted(const QPixmap &screenshot);
    void captureCancelled();

private slots:
    void onRegionSelected(const QPixmap &screenshot, const QPoint &globalPosition);
    void onSelectionCancelled();

private:
    QPointer<RegionSelector> m_regionSelector;
    PinWindowManager *m_pinManager;
    WindowDetector *m_windowDetector;
};

#endif // CAPTUREMANAGER_H
