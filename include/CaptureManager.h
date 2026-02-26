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
class ScrollCaptureSession;

class CaptureManager : public QObject
{
    Q_OBJECT

public:
    explicit CaptureManager(PinWindowManager *pinManager, QObject *parent = nullptr);
    ~CaptureManager();

    bool isActive() const;
    bool hasCompleteSelection() const;
    void triggerFinishSelection();
    void cancelCapture();
    void cycleOrSwitchCaptureScreenByCursor();

public slots:
    void startRegionCapture(bool showShortcutHintsOnEntry = false);
    void startQuickPinCapture();
    void startRegionCaptureWithPreset(const QRect &region, QScreen *screen);

signals:
    void captureStarted();
    void captureCompleted(const QPixmap &screenshot);
    void captureCancelled();
    void recordingRequested(const QRect &region, QScreen *screen);
    void saveCompleted(const QPixmap &screenshot, const QString &filePath);
    void saveFailed(const QString &filePath, const QString &error);

private slots:
    void onRegionSelected(const QPixmap &screenshot, const QPoint &globalPosition, const QRect &globalRect);
    void onSelectionCancelled();
    void onScrollingCaptureRequested(const QRect &region, QScreen *screen);

private:
    enum class CaptureEntryMode {
        Region,
        QuickPin
    };

    void startCaptureInternal(CaptureEntryMode mode, bool showShortcutHintsOnEntry);
    void initializeRegionSelector(QScreen *targetScreen,
                                  const QPixmap &preCapture,
                                  bool quickPinMode,
                                  bool showShortcutHintsOnEntry);

    QPointer<RegionSelector> m_regionSelector;
    QPointer<ScrollCaptureSession> m_scrollSession;
    PinWindowManager *m_pinManager;
    WindowDetector *m_windowDetector;
};

#endif // CAPTUREMANAGER_H
